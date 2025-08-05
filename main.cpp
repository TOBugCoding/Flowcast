#include "audioRecord.hpp"
#include "videoRecord.hpp"
#include "PacketQueue.hpp"
#include <thread>
#include <atomic>
#include <chrono>

// ȫ�ֶ���
PacketQueue packetQueue;
std::atomic<bool> writeThreadRunning(false);

// д���̺߳���
void write_thread_func(AVFormatContext* ofmtCtx) {
    writeThreadRunning = true;
    while (writeThreadRunning || !packetQueue.Empty()) {
        PacketWithStream packet = packetQueue.Pop();
        if (packet.pkt == nullptr) {
            // ��ֹ�ź�
            break;
        }
         // ȷ��ʱ�����Ч
        if (packet.stream->time_base.den == 30|| packet.stream->time_base.den == 60) {
			std::cout << "Invalid time_base, skipping packet" << std::endl;
            av_packet_free(&packet.pkt);
            continue;
        }
        av_interleaved_write_frame(ofmtCtx, packet.pkt);
        av_packet_free(&packet.pkt);
    }

    // д���ļ�β��
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
    // 1. �������������
    AVFormatContext* ofmtCtx = nullptr;
    avformat_alloc_output_context2(&ofmtCtx, nullptr, nullptr, outFile.c_str());
    if (!ofmtCtx) {
        std::cerr << "Could not create output context" << std::endl;
        return -1;
    }
    std::thread writeThread;
    // 2. �����Ƶ��
    AVStream* videoStream;
    // 3. �����Ƶ��
    AVStream* audioStream;
    // 4. ������ļ�
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

            // 2. �����Ƶ��
            videoStream = avformat_new_stream(ofmtCtx, nullptr);
            // 3. �����Ƶ��
            audioStream = avformat_new_stream(ofmtCtx, nullptr);

            // ������Ƶ����ʼ����
            audioStream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            audioStream->codecpar->codec_id = AV_CODEC_ID_AAC;
            audioStream->codecpar->sample_rate = 44100;
            audioStream->codecpar->ch_layout = AV_CHANNEL_LAYOUT_STEREO;


            // ������Ƶ����ʼ����
            videoStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            videoStream->codecpar->codec_id = AV_CODEC_ID_H264;
            videoStream->codecpar->width = 1920;
            videoStream->codecpar->height = 1080;
            videoStream->codecpar->format = AV_PIX_FMT_YUV420P;

            // ������Ƶ¼���߳�
            vr.setstop_recording(false);
            std::thread([&vr, videoStream] {
                vr.record_screen(nullptr, videoStream, packetQueue);
                }).detach();

            // ������Ƶ¼���߳�
            ar.setstop_recording(false);
            std::thread([&ar, audioStream] {
                ar.record_audio(nullptr, audioStream, packetQueue);
                }).detach();

            // �ȴ�¼���̳߳�ʼ�����
            std::this_thread::sleep_for(std::chrono::milliseconds(100));


            // д���ļ�ͷ��������������������ɺ�
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

                // �ȴ�¼�ƽ���
                while (vr.getrecording() || ar.getrecording()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                // ������ֹ�źŵ�����
                packetQueue.Push({ nullptr, nullptr });
            }

            // �ȴ�д���߳̽���
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