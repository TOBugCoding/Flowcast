#pragma once
#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <Windows.h>
#include "PacketQueue.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
#include <libavutil/time.h>
#include <libavutil/pixdesc.h>
}

class videoRecord {
public:
    void cleanup_resources(AVFormatContext* fmt_ctx, AVCodecContext* codec_ctx,
        AVFormatContext* out_ctx, AVFrame* frame, AVPacket* pkt,
        SwsContext* sws_ctx, AVFrame* tmp_frame, AVCodecContext* input_codec_ctx);
    void record_screen(AVFormatContext* ofmtCtx, AVStream* videoStream, PacketQueue& queue);
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