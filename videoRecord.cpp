#include "videoRecord.hpp"

void videoRecord::cleanup_resources(AVFormatContext* fmt_ctx, AVCodecContext* codec_ctx,
    AVFormatContext* out_ctx, AVFrame* frame, AVPacket* pkt,
    SwsContext* sws_ctx, AVFrame* tmp_frame, AVCodecContext* input_codec_ctx) {
    if (sws_ctx) sws_freeContext(sws_ctx);
    if (frame) av_frame_free(&frame);
    if (tmp_frame) av_frame_free(&tmp_frame);
    if (pkt) av_packet_free(&pkt);
    if (codec_ctx) avcodec_free_context(&codec_ctx);
    if (input_codec_ctx) avcodec_free_context(&input_codec_ctx);
    if (fmt_ctx) avformat_close_input(&fmt_ctx);
    if (out_ctx) {
        if (!(out_ctx->oformat->flags & AVFMT_NOFILE) && out_ctx->pb) {
            avio_closep(&out_ctx->pb);
        }
        avformat_free_context(out_ctx);
    }
}

/// @brief 录制视频主题函数
/// @param videoStream 主线程创建的视频流
void videoRecord::record_screen(AVFormatContext* /*outFormatCtx*/, AVStream* videoStream, PacketQueue& queue) {
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVCodecContext* input_codec_ctx = nullptr;
    const AVCodec* codec = nullptr;
    const AVCodec* input_codec = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* tmp_frame = nullptr;
    AVFrame* decoded_frame = nullptr;
    AVPacket* pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    AVStream* video_st = videoStream; // 使用传入的视频流
    int video_stream_idx = -1;
    int ret = 0;

    int64_t start_time = av_gettime();
    

    const AVInputFormat* input_fmt = av_find_input_format("gdigrab");
    if (!input_fmt) {
        std::cerr << "Could not find gdigrab input device" << std::endl;
        return;
    }

    AVDictionary* options = nullptr;
    av_dict_set(&options, "framerate", "60", 0);
    av_dict_set(&options, "video_size", "1920x1080", 0);
    av_dict_set(&options, "offset_x", "0", 0);
    av_dict_set(&options, "offset_y", "0", 0);
    av_dict_set(&options, "draw_mouse", "1", 0);
    if ((ret = avformat_open_input(&fmt_ctx, "desktop", input_fmt, &options))) {
        return;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, nullptr)) < 0) {
        cleanup_resources(fmt_ctx, codec_ctx, nullptr, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx);
        return;
    }

    for (int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }

    if (video_stream_idx == -1) {
        std::cerr << "Could not find video stream" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, nullptr, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx);
        return;
    }

    AVCodecParameters* codec_params = fmt_ctx->streams[video_stream_idx]->codecpar;
    AVPixelFormat input_pix_fmt = (AVPixelFormat)codec_params->format;

    input_codec = avcodec_find_decoder(codec_params->codec_id);
    input_codec_ctx = avcodec_alloc_context3(input_codec);
    ret = avcodec_parameters_to_context(input_codec_ctx, codec_params);
    ret = avcodec_open2(input_codec_ctx, input_codec, nullptr);
    if (ret < 0) {
        std::cerr << "Could not open decoder" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, nullptr, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx);
        return;
    }

    // 配置编码器
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "Could not find H.264 encoder" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, nullptr, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx);
        return;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Could not allocate codec context" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, nullptr, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx);
        return;
    }

    codec_ctx->width = codec_params->width;
    codec_ctx->height = codec_params->height;
    codec_ctx->framerate = { 60, 1 };
	codec_ctx->time_base = av_inv_q(codec_ctx->framerate);
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx->bit_rate = 4000000;
    codec_ctx->gop_size = 60;
  /*  codec_ctx->max_b_frames = 0;*/
   
    // 设置全局头标志
    codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
    av_opt_set(codec_ctx->priv_data, "crf", "25", 0);


    if ((ret = avcodec_open2(codec_ctx, codec, nullptr)) < 0) {
        std::cerr << "Failed to open encoder" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, nullptr, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx);
        return;
    }

    // 将编码器参数复制到主线程创建的流中
    avcodec_parameters_from_context(video_st->codecpar, codec_ctx);
    video_st->time_base = codec_ctx->time_base;

    frame = av_frame_alloc();
    tmp_frame = av_frame_alloc();
    decoded_frame = av_frame_alloc();
    pkt = av_packet_alloc();

    if (!frame || !tmp_frame || !decoded_frame || !pkt) {
        std::cerr << "Could not allocate frame or packet" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, nullptr, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx);
        return;
    }

    frame->width = codec_ctx->width;
    frame->height = codec_ctx->height;
    frame->format = codec_ctx->pix_fmt;

    if ((ret = av_frame_get_buffer(frame, 0)) < 0) {
        std::cerr << "Could not allocate frame data" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, nullptr, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx);
        return;
    }

    sws_ctx = sws_getContext(
        codec_params->width, codec_params->height, input_pix_fmt,
        codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
        SWS_BICUBIC, nullptr, nullptr, nullptr);

    if (!sws_ctx) {
        std::cerr << "Could not create sws context" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, nullptr, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx);
        return;
    }

    recording = true;
    stop_recording = false;
    std::cout << "Video recording started" << std::endl;

    while (!stop_recording) {
        ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) break;
            std::cerr << "Error reading frame" << std::endl;
            av_packet_unref(pkt);
            continue;
        }

        if (pkt->stream_index == video_stream_idx) {
            ret = avcodec_send_packet(input_codec_ctx, pkt);
            if (ret < 0) {
                std::cerr << "Error sending packet to decoder" << std::endl;
                av_packet_unref(pkt);
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(input_codec_ctx, decoded_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                else if (ret < 0) {
                    std::cerr << "Error receiving frame from decoder" << std::endl;
                    break;
                }

                sws_scale(sws_ctx,
                    decoded_frame->data, decoded_frame->linesize,
                    0, decoded_frame->height,
                    frame->data, frame->linesize);

                int64_t current_time = av_gettime() - start_time;
                frame->pts = av_rescale_q(current_time, AVRational{ 1, AV_TIME_BASE }, codec_ctx->time_base);

                ret = avcodec_send_frame(codec_ctx, frame);
                if (ret < 0) {
                    std::cerr << "Error sending frame to encoder" << std::endl;
                    continue;
                }

                while (true) {
                    ret = avcodec_receive_packet(codec_ctx, pkt);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    else if (ret < 0) {
                        std::cerr << "Error during encoding" << std::endl;
                        break;
                    }
                    if (!video_st || video_st->time_base.den == 0) {
                        av_packet_unref(pkt);
                        continue;   // 直接丢弃
                    }
                    av_packet_rescale_ts(pkt, codec_ctx->time_base, video_st->time_base);
                    pkt->stream_index = video_st->index;
                    // 将包放入队列
                    AVPacket* pktCopy = av_packet_alloc();
                    av_packet_ref(pktCopy, pkt);
                    queue.Push({ pktCopy, video_st });

                    av_packet_unref(pkt);
                }
            }
        }

        av_packet_unref(pkt);
    }

    avcodec_send_frame(codec_ctx, nullptr);
    while (true) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR_EOF) break;
        if (ret < 0) break;

        av_packet_rescale_ts(pkt, codec_ctx->time_base, video_st->time_base);
        pkt->stream_index = video_st->index;

        AVPacket* pktCopy = av_packet_alloc();
        av_packet_ref(pktCopy, pkt);
        queue.Push({ pktCopy, video_st });

        av_packet_unref(pkt);
    }

    cleanup_resources(fmt_ctx, codec_ctx, nullptr, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx);
    recording = false;
    std::cout << "Video recording stopped\n";
}