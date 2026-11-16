#pragma once
#include <vulkan/vulkan.h>
#include <vector>

struct ResourceState {
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
};

class BufferStateHandle {
    ResourceState m_state;
    VkBuffer m_buffer;

public:
    BufferStateHandle(): m_buffer(VK_NULL_HANDLE), m_state() {}
    BufferStateHandle(VkBuffer buffer, ResourceState state): m_buffer(buffer), m_state(state){}
    friend class BarrierManager;
};

class BarrierManager {
    std::vector<VkBufferMemoryBarrier2> m_barriers;

public:
    BarrierManager& enqueue(BufferStateHandle& buffer, ResourceState targetState) {

        // treat VK_PIPELINE_STAGE_2_NONE as "uninitialized". Save state, but don't issue a barrier 
        if (buffer.m_state.stage != VK_PIPELINE_STAGE_2_NONE) {
            m_barriers.push_back({ 
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .srcStageMask = buffer.m_state.stage,
                .srcAccessMask = buffer.m_state.access,
                .dstStageMask = targetState.stage,
                .dstAccessMask = targetState.access,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = buffer.m_buffer,
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });
        }

        buffer.m_state = targetState;

        return *this;
    }

    void barrier(VkCommandBuffer cmd) {

        if (m_barriers.size() == 0) {
            return;
        }

        VkDependencyInfo dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = static_cast<uint32_t>(m_barriers.size()),
            .pBufferMemoryBarriers = m_barriers.data()
        };

        vkCmdPipelineBarrier2(cmd, &dependency);
        m_barriers.clear();
    }
};