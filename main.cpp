#include "audioRecord.hpp"
#include "videoRecord.hpp"
#include "PacketQueue.hpp"
#include <thread>
#include <atomic>
#include <chrono>

// 全局队列
PacketQueue packetQueue;
std::atomic<bool> writeThreadRunning(false);

// 写入线程函数
void write_thread_func(AVFormatContext* ofmtCtx) {
    writeThreadRunning = true;
    while (writeThreadRunning || !packetQueue.Empty()) {
        PacketWithStream packet = packetQueue.Pop();
        if (packet.pkt == nullptr) {
            // 终止信号
            break;
        }
         // 确保时间戳有效
        if (packet.stream->time_base.den == 30|| packet.stream->time_base.den == 60) {
			std::cout << "Invalid time_base, skipping packet" << std::endl;
            av_packet_free(&packet.pkt);
            continue;
        }
        av_interleaved_write_frame(ofmtCtx, packet.pkt);
        av_packet_free(&packet.pkt);
    }

    // 写入文件尾部
    av_write_trailer(ofmtCtx);
    avio_closep(&ofmtCtx->pb);
    avformat_free_context(ofmtCtx);
    writeThreadRunning = false;
}



int main() {

    avdevice_register_all();
    audioRecord ar;
    videoRecord vr;
    std::string outFile = "D:\\zhongjinpeng\\recording.mp4";
    // 1. 创建输出上下文
    AVFormatContext* ofmtCtx = nullptr;
    avformat_alloc_output_context2(&ofmtCtx, nullptr, nullptr, outFile.c_str());
    if (!ofmtCtx) {
        std::cerr << "Could not create output context" << std::endl;
        return -1;
    }
    std::thread writeThread;
    // 2. 添加视频流
    AVStream* videoStream;
    // 3. 添加音频流
    AVStream* audioStream;
    // 4. 打开输出文件
    if (!(ofmtCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&ofmtCtx->pb, outFile.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cerr << "Could not open output file" << std::endl;
            return -1;
        }
    }



    std::cout << "Commands:\n"
        << "start  - begin recording\n"
        << "end    - stop recording\n"
        << "exit   - quit program\n";

    bool recording = false;
    std::string cmd;
    while (std::getline(std::cin, cmd)) {
        if (cmd == "start" && !recording) {

            writeThread = std::thread(write_thread_func, ofmtCtx);

            // 2. 添加视频流
            videoStream = avformat_new_stream(ofmtCtx, nullptr);
            // 3. 添加音频流
            audioStream = avformat_new_stream(ofmtCtx, nullptr);

            // 设置音频流初始参数
            audioStream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            audioStream->codecpar->codec_id = AV_CODEC_ID_AAC;
            audioStream->codecpar->sample_rate = 44100;
            audioStream->codecpar->ch_layout = AV_CHANNEL_LAYOUT_STEREO;


            // 设置视频流初始参数
            videoStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            videoStream->codecpar->codec_id = AV_CODEC_ID_H264;
            videoStream->codecpar->width = 1920;
            videoStream->codecpar->height = 1080;
            videoStream->codecpar->format = AV_PIX_FMT_YUV420P;

            // 启动视频录制线程
            vr.setstop_recording(false);
            std::thread([&vr, videoStream] {
                vr.record_screen(nullptr, videoStream, packetQueue);
                }).detach();

            // 启动音频录制线程
            ar.setstop_recording(false);
            std::thread([&ar, audioStream] {
                ar.record_audio(nullptr, audioStream, packetQueue);
                }).detach();

            // 等待录制线程初始化完成
            std::this_thread::sleep_for(std::chrono::milliseconds(100));


            // 写入文件头（必须在所有流配置完成后）
            if (avformat_write_header(ofmtCtx, nullptr) < 0) {
                std::cout << "Failed to write file header" << std::endl;
            }

            recording = true;
            std::cout << "Recording started..." << std::endl;
        }
        else if (cmd == "end") {
            if (recording) {
                vr.setstop_recording(true);
                ar.setstop_recording(true);

                // 等待录制结束
                while (vr.getrecording() || ar.getrecording()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                // 发送终止信号到队列
                packetQueue.Push({ nullptr, nullptr });
            }

            // 等待写入线程结束
            writeThreadRunning = false;
            packetQueue.Abort();
            if (writeThread.joinable()) {
                writeThread.join();
            }
            break;
        }
    }

    return 0;
}