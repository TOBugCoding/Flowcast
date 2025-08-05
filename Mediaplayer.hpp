#pragma once
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <Windows.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>
#include <libswresample/swresample.h>
}

class Mediaplayer {
public:
    void cleanup_resources(AVFormatContext* fmt_ctx, AVCodecContext* codec_ctx,
        AVFormatContext* out_ctx, AVFrame* frame, AVPacket* pkt,
        SwsContext* sws_ctx, AVFrame* tmp_frame, AVCodecContext* input_codec_ctx,
        AVFormatContext* audio_fmt_ctx, AVCodecContext* audio_dec_ctx,
        AVCodecContext* audio_enc_ctx, AVFrame* audio_frame);

    void record_screen(const std::string& output_path);

    bool getrecording() {
        return recording;
    }

    void setrecording(bool b) {
        recording = b;
    }

    bool getstop_recording() {
        return stop_recording;
    }

    void setstop_recording(bool b) {
        stop_recording = b;
    }
private:
    std::atomic<bool> recording = false;
    std::atomic<bool> stop_recording = false;
};