// PacketQueue.hpp
#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

struct PacketWithStream {
    AVPacket* pkt;
    AVStream* stream;  // ËùÊôÁ÷
};

class PacketQueue {
public:
    PacketQueue() : abort(false) {}

    void Push(PacketWithStream packet) {
        std::unique_lock<std::mutex> lock(mutex);
        queue.push(packet);
        cond.notify_one();
    }

    PacketWithStream Pop() {
        std::unique_lock<std::mutex> lock(mutex);
        while (queue.empty() && !abort) {
            cond.wait(lock);
        }

        if (abort) {
            return { nullptr, nullptr };
        }

        PacketWithStream packet = queue.front();
        queue.pop();
        return packet;
    }

    void Abort() {
        std::unique_lock<std::mutex> lock(mutex);
        abort = true;
        cond.notify_all();
    }

    bool Empty() const {
        std::unique_lock<std::mutex> lock(mutex);
        return queue.empty();
    }

private:
    std::queue<PacketWithStream> queue;
    mutable std::mutex mutex;
    std::condition_variable cond;
    std::atomic<bool> abort;
};