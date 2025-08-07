// shit_win_native.cpp - Windows native ugly video player using FFmpeg + SDL2
// THS IS THE WINDOWS VER. USE SHIT.CPP FOR THE LINUX ONE

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

#include <SDL.h>
#include <windows.h>  // for Windows stuff like DPI awareness

#include <iostream>

int main(int argc, char* argv[]) {
    // Fix DPI scaling issues on Windows 10+ for crisp window size
    SetProcessDPIAware();

    if (argc < 2) {
        std::cout << "Usage: shit_win_native <video_file>\n";
        return -1;
    }

    const char* filepath = argv[1];

    avformat_network_init();

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, filepath, nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file.\n";
        return -1;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream info.\n";
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    int video_stream_index = -1;
    for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }

    if (video_stream_index == -1) {
        std::cerr << "No video stream found.\n";
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_index]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "Codec not found.\n";
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Could not allocate codec context.\n";
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
        std::cerr << "Could not copy codec parameters.\n";
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Could not open codec.\n";
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    // Get screen size for fitting window
    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    int screen_w = dm.w;
    int screen_h = dm.h;

    int win_w = codec_ctx->width;
    int win_h = codec_ctx->height;

    if (win_w > screen_w || win_h > screen_h) {
        float w_ratio = (float)screen_w / win_w;
        float h_ratio = (float)screen_h / win_h;
        float scale = (w_ratio < h_ratio) ? w_ratio : h_ratio;
        win_w = (int)(win_w * scale);
        win_h = (int)(win_h * scale);
    }

    SDL_Window* window = SDL_CreateWindow(
        "shit_win_native",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        win_w,
        win_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_YV12,
        SDL_TEXTUREACCESS_STREAMING,
        codec_ctx->width,
        codec_ctx->height
    );

    if (!texture) {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return -1;
    }

    AVFrame* frame = av_frame_alloc();
    AVFrame* yuv_frame = av_frame_alloc();

    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, codec_ctx->width, codec_ctx->height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t));
    av_image_fill_arrays(yuv_frame->data, yuv_frame->linesize, buffer, AV_PIX_FMT_YUV420P, codec_ctx->width, codec_ctx->height, 1);

    SwsContext* sws_ctx = sws_getContext(
        codec_ctx->width,
        codec_ctx->height,
        codec_ctx->pix_fmt,
        codec_ctx->width,
        codec_ctx->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        nullptr, nullptr, nullptr
    );

    AVPacket* packet = av_packet_alloc();

    SDL_Event event;
    bool quit = false;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                quit = true;
        }

        if (av_read_frame(fmt_ctx, packet) >= 0) {
            if (packet->stream_index == video_stream_index) {
                if (avcodec_send_packet(codec_ctx, packet) == 0) {
                    while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                        sws_scale(
                            sws_ctx,
                            (const uint8_t* const*)frame->data,
                            frame->linesize,
                            0,
                            codec_ctx->height,
                            yuv_frame->data,
                            yuv_frame->linesize
                        );

                        SDL_UpdateYUVTexture(
                            texture,
                            nullptr,
                            yuv_frame->data[0], yuv_frame->linesize[0],
                            yuv_frame->data[1], yuv_frame->linesize[1],
                            yuv_frame->data[2], yuv_frame->linesize[2]
                        );

                        SDL_RenderClear(renderer);
                        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
                        SDL_RenderPresent(renderer);

                        SDL_Delay(1000 / 30); // crude ~30 FPS
                    }
                }
            }
            av_packet_unref(packet);
        } else {
            quit = true; // EOF
        }
    }

    av_packet_free(&packet);
    av_free(buffer);
    av_frame_free(&yuv_frame);
    av_frame_free(&frame);
    sws_freeContext(sws_ctx);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    return 0;
}
