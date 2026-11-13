#pragma once
#include <vulkan/vulkan.h>

struct ResourceUsage {
    VkAccessFlags access{VK_ACCESS_NONE};
    VkPipelineStageFlags stage{VK_PIPELINE_STAGE_NONE};
};

class ResourceHandle {
    VkDescriptorBufferInfo m_bufferInfo;
    ResourceUsage m_lastUsage;
    
    ResourceHandle(){}

public:
    static ResourceHandle makeBuffer(VkBuffer buffer) {
        ResourceHandle handle;
        handle.m_bufferInfo.buffer = buffer;
        handle.m_bufferInfo.offset = 0;
        handle.m_bufferInfo.range = VK_WHOLE_SIZE;
        return handle;
    }

    const VkDescriptorBufferInfo* bufferInfo() const {
        return &m_bufferInfo;
    }
};