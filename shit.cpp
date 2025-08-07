extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}
#include <SDL2/SDL.h>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./shit_player <video_file>\n";
        return -1;
    }

    const char* filepath = argv[1];

    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, filepath, nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file.\n";
        return -1;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream info.\n";
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
        return -1;
    }

    AVCodecParameters* codecpar = fmt_ctx->streams[video_stream_index]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "Unsupported codec.\n";
        return -1;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, codecpar);
    avcodec_open2(codec_ctx, codec, nullptr);

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

    // Get screen size
    SDL_DisplayMode dm;
    SDL_GetCurrentDisplayMode(0, &dm);
    int screen_w = dm.w;
    int screen_h = dm.h;

    int video_w = codec_ctx->width;
    int video_h = codec_ctx->height;

    // Calculate scale to fit screen (preserve aspect ratio)
    float scale_x = (float)screen_w / video_w;
    float scale_y = (float)screen_h / video_h;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    int win_w = (int)(video_w * scale);
    int win_h = (int)(video_h * scale);

    SDL_Window* window = SDL_CreateWindow(
        "shitty_player",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        win_w,
        win_h,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_YV12,
        SDL_TEXTUREACCESS_STREAMING,
        codec_ctx->width,
        codec_ctx->height
    );

    AVFrame* frame = av_frame_alloc();
    AVFrame* yuv = av_frame_alloc();

    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, codec_ctx->width, codec_ctx->height, 1);
    uint8_t* buffer = (uint8_t*)av_malloc(num_bytes * sizeof(uint8_t));
    av_image_fill_arrays(yuv->data, yuv->linesize, buffer, AV_PIX_FMT_YUV420P, codec_ctx->width, codec_ctx->height, 1);

    struct SwsContext* sws_ctx = sws_getContext(
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
                            (uint8_t const* const*)frame->data,
                            frame->linesize,
                            0,
                            codec_ctx->height,
                            yuv->data,
                            yuv->linesize
                        );

                        SDL_UpdateYUVTexture(
                            texture,
                            nullptr,
                            yuv->data[0], yuv->linesize[0],
                            yuv->data[1], yuv->linesize[1],
                            yuv->data[2], yuv->linesize[2]
                        );

                        SDL_RenderClear(renderer);

                        SDL_Rect dest_rect;
                        dest_rect.x = (win_w - (int)(video_w * scale)) / 2;
                        dest_rect.y = (win_h - (int)(video_h * scale)) / 2;
                        dest_rect.w = (int)(video_w * scale);
                        dest_rect.h = (int)(video_h * scale);

                        SDL_RenderCopy(renderer, texture, nullptr, &dest_rect);
                        SDL_RenderPresent(renderer);

                        SDL_Delay(1000 / 30);  // crude ~30 FPS
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
    av_frame_free(&yuv);
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
