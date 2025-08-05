#include "audioRecord.hpp"

/* ---------- 工具：释放资源 ---------- */
void audioRecord::cleanup(AVFormatContext** ictx,
    AVCodecContext** dctx,
    AVFormatContext** octx,
    AVCodecContext** ectx,
    SwrContext** swr,
    AVAudioFifo** fifo,
    AVFrame** frame,
    AVFrame** resFrm,
    AVPacket** pkt) {
    if (fifo && *fifo) { av_audio_fifo_free(*fifo); *fifo = nullptr; }
    if (swr && *swr) { swr_free(swr); *swr = nullptr; }
    if (frame && *frame) { av_frame_free(frame); *frame = nullptr; }
    if (resFrm && *resFrm) { av_frame_free(resFrm); *resFrm = nullptr; }
    if (pkt && *pkt) { av_packet_free(pkt); *pkt = nullptr; }
    if (ectx && *ectx) { avcodec_free_context(ectx); *ectx = nullptr; }
    if (dctx && *dctx) { avcodec_free_context(dctx); *dctx = nullptr; }

    if (octx && *octx) {
        if (!((*octx)->oformat->flags & AVFMT_NOFILE) && (*octx)->pb)
            avio_closep(&(*octx)->pb);
        avformat_free_context(*octx);
        *octx = nullptr;
    }

    if (ictx && *ictx) { avformat_close_input(ictx); *ictx = nullptr; }
}

/* ---------- 核心：录音 ---------- */
void audioRecord::record_audio(AVFormatContext* /*ofmtCtx*/, AVStream* audioStream, PacketQueue& queue) {
   
    AVFormatContext* ifmtCtx = nullptr;
    AVCodecContext* decCtx = nullptr;
    AVCodecContext* encCtx = nullptr;
    SwrContext* swrCtx = nullptr;
    AVAudioFifo* fifo = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* resFrm = nullptr;
    AVPacket* pkt = nullptr;
    int ret;

    /* 1. 打开麦克风 */
    const AVInputFormat* ifmt = av_find_input_format("dshow");
    if (!ifmt) { std::cerr << "DirectShow not found\n"; return; }

    AVDictionary* opt = nullptr;
    av_dict_set(&opt, "sample_rate", "44100", 0);
    av_dict_set(&opt, "channels", "2", 0);
    av_dict_set(&opt, "sample_fmt", "s16", 0);

    ret = avformat_open_input(&ifmtCtx, "audio=Microphone (Conexant ISST Audio)", ifmt, &opt);
    if (ret < 0) { std::cerr << "open mic failed\n"; return; }

    ret = avformat_find_stream_info(ifmtCtx, nullptr);
    if (ret < 0) { cleanup(&ifmtCtx, &decCtx, nullptr, &encCtx, &swrCtx, &fifo, &frame, &resFrm, &pkt); return; }

    int audioIdx = av_find_best_stream(ifmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audioIdx < 0) { cleanup(&ifmtCtx, &decCtx, nullptr, &encCtx, &swrCtx, &fifo, &frame, &resFrm, &pkt); return; }

    /* 2. 解码器 */
    AVCodecParameters* par = ifmtCtx->streams[audioIdx]->codecpar;
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    decCtx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(decCtx, par);
    avcodec_open2(decCtx, dec, nullptr);

    /* 3. 编码器 & 输出 */
    const AVCodec* enc = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!enc) {
        std::cerr << "AAC encoder not found\n";
        cleanup(&ifmtCtx, &decCtx, nullptr, &encCtx, &swrCtx, &fifo, &frame, &resFrm, &pkt);
        return;
    }

    encCtx = avcodec_alloc_context3(enc);
    encCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    encCtx->bit_rate = 128000;
    encCtx->sample_rate = 44100;
    encCtx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    encCtx->time_base = { 1, 44100 };
  
    encCtx->frame_size = 1024;
    // 设置全局头标志
    encCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(encCtx, enc, nullptr) < 0) {
        std::cerr << "Failed to open audio encoder\n";
        cleanup(&ifmtCtx, &decCtx, nullptr, &encCtx, &swrCtx, &fifo, &frame, &resFrm, &pkt);
        return;
    }

    // 将编码器参数复制到主线程创建的流中
    avcodec_parameters_from_context(audioStream->codecpar, encCtx);
    audioStream->time_base = encCtx->time_base;

    /* 4. 重采样上下文 */
    swrCtx = swr_alloc();
    if (!swrCtx) {
        std::cerr << "swr_alloc failed\n";
        cleanup(&ifmtCtx, &decCtx, nullptr, &encCtx, &swrCtx, &fifo, &frame, &resFrm, &pkt);
        return;
    }
    av_opt_set_chlayout(swrCtx, "in_chlayout", &decCtx->ch_layout, 0);
    av_opt_set_chlayout(swrCtx, "out_chlayout", &encCtx->ch_layout, 0);
    av_opt_set_int(swrCtx, "in_sample_rate", decCtx->sample_rate, 0);
    av_opt_set_int(swrCtx, "out_sample_rate", encCtx->sample_rate, 0);
    av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", decCtx->sample_fmt, 0);
    av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", encCtx->sample_fmt, 0);
    swr_init(swrCtx);

    /* 5. FIFO + 主循环 */
    fifo = av_audio_fifo_alloc(encCtx->sample_fmt,
        encCtx->ch_layout.nb_channels,
        1024 * 8);

    frame = av_frame_alloc();
    resFrm = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!frame || !resFrm || !pkt) {
        std::cerr << "Failed to allocate frame or packet\n";
        cleanup(&ifmtCtx, &decCtx, nullptr, &encCtx, &swrCtx, &fifo, &frame, &resFrm, &pkt);
        return;
    }

    int64_t pts = 0;
    std::cout << "Audio recording started\n";
    recording = true;
    stop_recording = false;

    while (!stop_recording.load()) {
        ret = av_read_frame(ifmtCtx, pkt);
        if (ret < 0) break;
        if (pkt->stream_index != audioIdx) { av_packet_unref(pkt); continue; }

        ret = avcodec_send_packet(decCtx, pkt);
        av_packet_unref(pkt);
        if (ret < 0) continue;

        while (avcodec_receive_frame(decCtx, frame) == 0) {
            /* 计算最大输出样本数 */
            int outSamples = swr_get_out_samples(swrCtx, frame->nb_samples);
            av_frame_unref(resFrm);
            resFrm->nb_samples = outSamples;
            resFrm->ch_layout = encCtx->ch_layout;
            resFrm->sample_rate = encCtx->sample_rate;
            resFrm->format = encCtx->sample_fmt;
            av_frame_get_buffer(resFrm, 0);

            /* 重采样 */
            int got = swr_convert(swrCtx, resFrm->data, outSamples,
                (const uint8_t**)frame->data, frame->nb_samples);
            if (got < 0) continue;

            /* 写入 FIFO */
            av_audio_fifo_write(fifo, (void**)resFrm->data, got);

            /* 取出 1024 样本/帧 */
            while (av_audio_fifo_size(fifo) >= encCtx->frame_size) {
                AVFrame* out = av_frame_alloc();
                out->nb_samples = encCtx->frame_size;
                out->ch_layout = encCtx->ch_layout;
                out->sample_rate = encCtx->sample_rate;
                out->format = encCtx->sample_fmt;
                av_frame_get_buffer(out, 0);

                av_audio_fifo_read(fifo, (void**)out->data, encCtx->frame_size);
                out->pts = pts;
                pts += encCtx->frame_size;

                avcodec_send_frame(encCtx, out);
                av_frame_free(&out);

                while (avcodec_receive_packet(encCtx, pkt) == 0) {
                    if (!audioStream || audioStream->time_base.den == 0) {
                        av_packet_unref(pkt);
                        continue;   // 直接丢弃
                    }
                    av_packet_rescale_ts(pkt, encCtx->time_base, audioStream->time_base);
                    pkt->stream_index = audioStream->index;

                    // 确保时间戳有效
                    if (pkt->pts == AV_NOPTS_VALUE) {
                        pkt->pts = 0;
                    }
                    if (pkt->dts == AV_NOPTS_VALUE) {
                        pkt->dts = pkt->pts;
                    }

                    // 将包放入队列
                    AVPacket* pktCopy = av_packet_alloc();
                    av_packet_ref(pktCopy, pkt);
                    queue.Push({ pktCopy, audioStream });

                    av_packet_unref(pkt);
                }
            }
            av_frame_unref(frame);
        }
    }

    /* 6. flush FIFO & encoder */
    while (av_audio_fifo_size(fifo) > 0) {
        int left = av_audio_fifo_size(fifo);
        AVFrame* out = av_frame_alloc();
        out->nb_samples = left;
        out->ch_layout = encCtx->ch_layout;
        out->sample_rate = encCtx->sample_rate;
        out->format = encCtx->sample_fmt;
        av_frame_get_buffer(out, 0);

        av_audio_fifo_read(fifo, (void**)out->data, left);
        out->pts = pts;
        pts += left;

        avcodec_send_frame(encCtx, out);
        av_frame_free(&out);

        while (avcodec_receive_packet(encCtx, pkt) == 0) {
            av_packet_rescale_ts(pkt, encCtx->time_base, audioStream->time_base);
            pkt->stream_index = audioStream->index;

            AVPacket* pktCopy = av_packet_alloc();
            av_packet_ref(pktCopy, pkt);
            queue.Push({ pktCopy, audioStream });

            av_packet_unref(pkt);
        }
    }

    avcodec_send_frame(encCtx, nullptr); // flush
    while (avcodec_receive_packet(encCtx, pkt) == 0) {
        av_packet_rescale_ts(pkt, encCtx->time_base, audioStream->time_base);
        pkt->stream_index = audioStream->index;

        AVPacket* pktCopy = av_packet_alloc();
        av_packet_ref(pktCopy, pkt);
        queue.Push({ pktCopy, audioStream });

        av_packet_unref(pkt);
    }

    cleanup(&ifmtCtx, &decCtx, nullptr, &encCtx, &swrCtx, &fifo, &frame, &resFrm, &pkt);
    recording = false;
    std::cout << "Audio recording stopped\n";
}