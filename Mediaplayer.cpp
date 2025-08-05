#include "Mediaplayer.hpp"

void Mediaplayer::cleanup_resources(AVFormatContext* fmt_ctx, AVCodecContext* codec_ctx,
    AVFormatContext* out_ctx, AVFrame* frame, AVPacket* pkt,
    SwsContext* sws_ctx, AVFrame* tmp_frame, AVCodecContext* input_codec_ctx,
    AVFormatContext* audio_fmt_ctx, AVCodecContext* audio_dec_ctx,
    AVCodecContext* audio_enc_ctx, AVFrame* audio_frame) {

    if (sws_ctx) sws_freeContext(sws_ctx);
    if (frame) av_frame_free(&frame);
    if (tmp_frame) av_frame_free(&tmp_frame);
    if (audio_frame) av_frame_free(&audio_frame);
    if (pkt) av_packet_free(&pkt);
    if (codec_ctx) avcodec_free_context(&codec_ctx);
    if (input_codec_ctx) avcodec_free_context(&input_codec_ctx);
    if (audio_dec_ctx) avcodec_free_context(&audio_dec_ctx);
    if (audio_enc_ctx) avcodec_free_context(&audio_enc_ctx);
    if (fmt_ctx) avformat_close_input(&fmt_ctx);
    if (audio_fmt_ctx) avformat_close_input(&audio_fmt_ctx);
    if (out_ctx) {
        if (!(out_ctx->oformat->flags & AVFMT_NOFILE) && out_ctx->pb) {
            avio_closep(&out_ctx->pb);
        }
        avformat_free_context(out_ctx);
    }
}

void Mediaplayer::record_screen(const std::string& output_path) {
    AVFormatContext* fmt_ctx = nullptr;
    AVFormatContext* audio_fmt_ctx = nullptr;
    AVFormatContext* out_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    AVCodecContext* input_codec_ctx = nullptr;
    AVCodecContext* audio_dec_ctx = nullptr;
    AVCodecContext* audio_enc_ctx = nullptr;
    const AVCodec* codec = nullptr;
    const AVCodec* input_codec = nullptr;
    const AVCodec* audio_dec = nullptr;
    const AVCodec* audio_enc = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* tmp_frame = nullptr;
    AVFrame* decoded_frame = nullptr;
    AVFrame* audio_frame = nullptr;
    AVPacket* pkt = nullptr;
    AVPacket* audio_pkt = nullptr;
    SwsContext* sws_ctx = nullptr;
    AVStream* video_st = nullptr;
    AVStream* audio_st = nullptr;
    int video_stream_idx = -1;
    int audio_stream_idx = -1;
    int ret = 0;
    int64_t start_time = av_gettime();
    int frame_index = 0;

    avdevice_register_all();

    // ====================== 视频设备初始化 ======================
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
        std::cerr << "Could not open input device (error: " << ret << ")" << std::endl;
        return;
    }

    if ((ret = avformat_find_stream_info(fmt_ctx, nullptr)) < 0) {
        std::cerr << "Could not find stream info (error: " << ret << ")" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
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
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    AVCodecParameters* codec_params = fmt_ctx->streams[video_stream_idx]->codecpar;
    AVPixelFormat input_pix_fmt = (AVPixelFormat)codec_params->format;

    std::cout << "Input pixel format: " << av_get_pix_fmt_name(input_pix_fmt) << std::endl;
    std::cout << "Video size: " << codec_params->width << "x" << codec_params->height << std::endl;

    // 初始化视频解码器
    input_codec = avcodec_find_decoder(codec_params->codec_id);
    if (!input_codec) {
        std::cerr << "Could not find decoder" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    input_codec_ctx = avcodec_alloc_context3(input_codec);
    if (!input_codec_ctx) {
        std::cerr << "Could not allocate decoder context" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    ret = avcodec_parameters_to_context(input_codec_ctx, codec_params);
    if (ret < 0) {
        std::cerr << "Could not copy decoder parameters" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    ret = avcodec_open2(input_codec_ctx, input_codec, nullptr);
    if (ret < 0) {
        std::cerr << "Could not open decoder" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    // ====================== 音频设备初始化 ======================
    const AVInputFormat* audio_input_fmt = av_find_input_format("dshow");
    if (!audio_input_fmt) {
        std::cerr << "Could not find dshow input device" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    AVDictionary* audio_options = nullptr;
    av_dict_set(&audio_options, "sample_rate", "44100", 0);
    av_dict_set(&audio_options, "channels", "2", 0);
    av_dict_set(&audio_options, "sample_fmt", "s16", 0);

    // 使用您的设备名称
    if ((ret = avformat_open_input(&audio_fmt_ctx, "audio=Microphone (Conexant ISST Audio)", audio_input_fmt, &audio_options))) {
        std::cerr << "Could not open audio device (error: " << ret << ")" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    if ((ret = avformat_find_stream_info(audio_fmt_ctx, nullptr)) < 0) {
        std::cerr << "Could not find audio stream info (error: " << ret << ")" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    for (int i = 0; i < audio_fmt_ctx->nb_streams; i++) {
        if (audio_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_idx = i;
            break;
        }
    }

    if (audio_stream_idx == -1) {
        std::cerr << "Could not find audio stream" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    AVCodecParameters* audio_codecpar = audio_fmt_ctx->streams[audio_stream_idx]->codecpar;
    audio_dec = avcodec_find_decoder(audio_codecpar->codec_id);
    if (!audio_dec) {
        std::cerr << "Unsupported audio codec" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    audio_dec_ctx = avcodec_alloc_context3(audio_dec);
    if (!audio_dec_ctx) {
        std::cerr << "Failed to allocate audio decoder context" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    ret = avcodec_parameters_to_context(audio_dec_ctx, audio_codecpar);
    if (ret < 0) {
        std::cerr << "Failed to copy audio decoder parameters" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    ret = avcodec_open2(audio_dec_ctx, audio_dec, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to open audio decoder" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    // ====================== 输出文件初始化 ======================
    if ((ret = avformat_alloc_output_context2(&out_ctx, nullptr, nullptr, output_path.c_str())) < 0) {
        std::cerr << "Could not create output context (error: " << ret << ")" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    // 添加视频流
    video_st = avformat_new_stream(out_ctx, nullptr);
    if (!video_st) {
        std::cerr << "Could not create video stream" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "Could not find H.264 encoder" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Could not allocate codec context" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    codec_ctx->width = codec_params->width;
    codec_ctx->height = codec_params->height;
    codec_ctx->time_base = { 1, 60 };
    codec_ctx->framerate = { 60, 1 };
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx->bit_rate = 4000000;
    codec_ctx->gop_size = 250;
    codec_ctx->max_b_frames = 0;

    av_opt_set(codec_ctx->priv_data, "preset", "medium", 0);
    av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
    av_opt_set(codec_ctx->priv_data, "crf", "23", 0);

    if ((ret = avcodec_open2(codec_ctx, codec, nullptr)) < 0) {
        std::cerr << "Failed to open video encoder (error: " << ret << ")" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    if ((ret = avcodec_parameters_from_context(video_st->codecpar, codec_ctx)) < 0) {
        std::cerr << "Could not copy video codec parameters (error: " << ret << ")" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    video_st->time_base = codec_ctx->time_base;

    // 添加音频流
    audio_st = avformat_new_stream(out_ctx, nullptr);
    if (!audio_st) {
        std::cerr << "Could not create audio stream" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    audio_enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!audio_enc) {
        std::cerr << "Could not find AAC encoder" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    audio_enc_ctx = avcodec_alloc_context3(audio_enc);
    if (!audio_enc_ctx) {
        std::cerr << "Failed to allocate audio encoder context" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    audio_enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    audio_enc_ctx->bit_rate = 128000;
    audio_enc_ctx->sample_rate = 44100;
    audio_enc_ctx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    audio_enc_ctx->time_base = { 1, audio_enc_ctx->sample_rate };

    if (out_ctx->oformat->flags & AVFMT_GLOBALHEADER)
        audio_enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(audio_enc_ctx, audio_enc, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to open audio encoder (error: " << ret << ")" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    ret = avcodec_parameters_from_context(audio_st->codecpar, audio_enc_ctx);
    if (ret < 0) {
        std::cerr << "Failed to copy audio encoder parameters (error: " << ret << ")" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    audio_st->time_base = audio_enc_ctx->time_base;

    // 打开输出文件
    if (!(out_ctx->oformat->flags & AVFMT_NOFILE)) {
        if ((ret = avio_open(&out_ctx->pb, output_path.c_str(), AVIO_FLAG_WRITE))) {
            std::cerr << "Could not open output file (error: " << ret << ")" << std::endl;
            cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
                audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
            return;
        }
    }

    if ((ret = avformat_write_header(out_ctx, nullptr)) < 0) {
        std::cerr << "Could not write header (error: " << ret << ")" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    // 初始化视频帧
    frame = av_frame_alloc();
    tmp_frame = av_frame_alloc();
    decoded_frame = av_frame_alloc();
    pkt = av_packet_alloc();

    if (!frame || !tmp_frame || !decoded_frame || !pkt) {
        std::cerr << "Could not allocate video frame or packet" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    frame->width = codec_ctx->width;
    frame->height = codec_ctx->height;
    frame->format = codec_ctx->pix_fmt;

    if ((ret = av_frame_get_buffer(frame, 0)) < 0) {
        std::cerr << "Could not allocate video frame data (error: " << ret << ")" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    // 初始化音频帧
    audio_frame = av_frame_alloc();
    audio_pkt = av_packet_alloc();
    if (!audio_frame || !audio_pkt) {
        std::cerr << "Could not allocate audio frame or packet" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    // 初始化视频缩放上下文
    sws_ctx = sws_getContext(
        codec_params->width, codec_params->height, input_pix_fmt,
        codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
        SWS_BICUBIC, nullptr, nullptr, nullptr);

    if (!sws_ctx) {
        std::cerr << "Could not create sws context" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    // 初始化音频重采样上下文
    SwrContext* swr_ctx = swr_alloc();
    if (!swr_ctx) {
        std::cerr << "Could not allocate resampler context" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        return;
    }

    // 设置音频重采样参数
    av_opt_set_chlayout(swr_ctx, "in_chlayout", &audio_dec_ctx->ch_layout, 0);
    av_opt_set_chlayout(swr_ctx, "out_chlayout", &audio_enc_ctx->ch_layout, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", audio_dec_ctx->sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", audio_enc_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", audio_dec_ctx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", audio_enc_ctx->sample_fmt, 0);

    if ((ret = swr_init(swr_ctx)) < 0) {
        std::cerr << "Failed to initialize the resampling context" << std::endl;
        cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
            audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
        swr_free(&swr_ctx);
        return;
    }

    recording = true;
    stop_recording = false;
    std::cout << "Recording started with audio. Type 'end' to stop." << std::endl;

    while (!stop_recording) {
        // 处理视频帧
        ret = av_read_frame(fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) break;
            av_packet_unref(pkt);
        }
        else if (pkt->stream_index == video_stream_idx) {
            ret = avcodec_send_packet(input_codec_ctx, pkt);
            if (ret < 0) {
                av_packet_unref(pkt);
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(input_codec_ctx, decoded_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                else if (ret < 0) {
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
                    continue;
                }

                while (true) {
                    ret = avcodec_receive_packet(codec_ctx, pkt);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    else if (ret < 0) {
                        break;
                    }

                    av_packet_rescale_ts(pkt, codec_ctx->time_base, video_st->time_base);
                    pkt->stream_index = video_st->index;

                    ret = av_interleaved_write_frame(out_ctx, pkt);
                    av_packet_unref(pkt);
                }
                av_frame_unref(decoded_frame);
            }
            av_packet_unref(pkt);
        }
        else {
            av_packet_unref(pkt);
        }

        // 处理音频帧
        ret = av_read_frame(audio_fmt_ctx, audio_pkt);
        if (ret < 0) {
            if (ret != AVERROR_EOF) {
                // 忽略非EOF错误
            }
        }
        else if (audio_pkt->stream_index == audio_stream_idx) {
            ret = avcodec_send_packet(audio_dec_ctx, audio_pkt);
            av_packet_unref(audio_pkt);
            if (ret < 0) {
                continue;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(audio_dec_ctx, audio_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                }
                else if (ret < 0) {
                    break;
                }

                // 配置重采样输出帧
                AVFrame* resampled_frame = av_frame_alloc();
                resampled_frame->sample_rate = audio_enc_ctx->sample_rate;
                resampled_frame->ch_layout = audio_enc_ctx->ch_layout;
                resampled_frame->format = audio_enc_ctx->sample_fmt;
                resampled_frame->nb_samples = audio_frame->nb_samples;
                av_frame_get_buffer(resampled_frame, 0);

                // 执行重采样
                ret = swr_convert_frame(swr_ctx, resampled_frame, audio_frame);
                if (ret < 0) {
                    av_frame_free(&resampled_frame);
                    av_frame_unref(audio_frame);
                    continue;
                }

                // 设置时间戳
                resampled_frame->pts = av_rescale_q(audio_frame->pts,
                    audio_dec_ctx->time_base, audio_enc_ctx->time_base);

                // 发送到编码器
                ret = avcodec_send_frame(audio_enc_ctx, resampled_frame);
                av_frame_free(&resampled_frame);
                av_frame_unref(audio_frame);
                if (ret < 0) {
                    continue;
                }

                while (true) {
                    ret = avcodec_receive_packet(audio_enc_ctx, audio_pkt);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                        break;
                    }
                    else if (ret < 0) {
                        break;
                    }

                    av_packet_rescale_ts(audio_pkt, audio_enc_ctx->time_base, audio_st->time_base);
                    audio_pkt->stream_index = audio_st->index;

                    ret = av_interleaved_write_frame(out_ctx, audio_pkt);
                    av_packet_unref(audio_pkt);
                }
            }
        }
        else {
            av_packet_unref(audio_pkt);
        }
    }

    // 刷新视频编码器
    avcodec_send_frame(codec_ctx, nullptr);
    while (true) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR_EOF) break;
        if (ret < 0) break;

        av_packet_rescale_ts(pkt, codec_ctx->time_base, video_st->time_base);
        pkt->stream_index = video_st->index;

        av_interleaved_write_frame(out_ctx, pkt);
        av_packet_unref(pkt);
    }

    // 刷新音频编码器
    avcodec_send_frame(audio_enc_ctx, nullptr);
    while (true) {
        ret = avcodec_receive_packet(audio_enc_ctx, audio_pkt);
        if (ret == AVERROR_EOF) break;
        if (ret < 0) break;

        av_packet_rescale_ts(audio_pkt, audio_enc_ctx->time_base, audio_st->time_base);
        audio_pkt->stream_index = audio_st->index;

        av_interleaved_write_frame(out_ctx, audio_pkt);
        av_packet_unref(audio_pkt);
    }

    av_write_trailer(out_ctx);
    cleanup_resources(fmt_ctx, codec_ctx, out_ctx, frame, pkt, sws_ctx, tmp_frame, input_codec_ctx,
        audio_fmt_ctx, audio_dec_ctx, audio_enc_ctx, audio_frame);
    swr_free(&swr_ctx);

    recording = false;
    std::cout << "Recording stopped. Video with audio saved to: " << output_path << std::endl;
}