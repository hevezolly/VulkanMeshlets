#pragma once
#include "VulkanBuffer.h"

class DelayedDestroyQueue {
    std::vector<std::vector<vks::Buffer>> m_delayedQueue;
    uint32_t m_currentFrame;

public:
    DelayedDestroyQueue(): m_currentFrame(0), m_delayedQueue(){}

    void pump(uint32_t frameId) {
        m_currentFrame = frameId;

        if (m_delayedQueue.size() <= m_currentFrame) {
            return;
        }

        for (auto& buffer: m_delayedQueue[m_currentFrame]) {
            buffer.destroy();
        }

        m_delayedQueue[m_currentFrame].clear();
    }

    void flush() {
        for (auto& queue: m_delayedQueue) {
            for (auto& buffer : queue) {
                buffer.destroy();
            }
        }

        m_delayedQueue.clear();
    }

    void enqueue(const vks::Buffer& buffer) {
        if (m_delayedQueue.size() <= m_currentFrame) {
            m_delayedQueue.resize(m_currentFrame + 1);
        }

        m_delayedQueue[m_currentFrame].push_back(buffer);
    }
};