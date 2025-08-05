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
#include <libswresample/swresample.h>
#include <libavutil/audio_fifo.h>
}

class audioRecord {
public:
    void cleanup(AVFormatContext** ictx,
        AVCodecContext** dctx,
        AVFormatContext** octx,
        AVCodecContext** ectx,
        SwrContext** swr,
        AVAudioFifo** fifo,
        AVFrame** frame,
        AVFrame** resFrm,  // 新增resFrm清理
        AVPacket** pkt);
    void record_audio(AVFormatContext* ofmtCtx, AVStream* audioStream, PacketQueue& queue);
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