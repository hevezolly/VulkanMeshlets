#pragma once
#include <vulkan/vulkan.h>
#include "descriptors.h"
#include "VulkanTools.h"

class ComputePass {
    VkPipeline m_pipeline;
    VkPipelineLayout m_layout;
    VkDevice m_device;
    uint32_t m_pushConstantSize;

public:

    ComputePass(): m_device(VK_NULL_HANDLE), m_pipeline(VK_NULL_HANDLE), m_layout(VK_NULL_HANDLE) {}

    ComputePass(
        VkDevice device, 
        const VkPipelineShaderStageCreateInfo& shaderStageInfo, 
        std::initializer_list<DescriptorSetLayout> layouts = {},
        uint32_t pushConstantSize = 0 // go with a single push constant range just for an ease of use
    ): m_device(device), m_pushConstantSize(pushConstantSize) {

        std::vector<VkDescriptorSetLayout> collectedLayouts{};

        for (const auto& l: layouts) {
            collectedLayouts.push_back(*l.layout());
        }        

        VkPipelineLayoutCreateInfo layoutInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<uint32_t>(collectedLayouts.size()),
            .pSetLayouts = collectedLayouts.data(),    
        };

        VkPushConstantRange pushConstantRange;

        if (pushConstantSize > 0) {
            pushConstantRange.offset = 0;
            pushConstantRange.size = pushConstantSize;
            pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushConstantRange;
        }

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout));
        
        VkComputePipelineCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = shaderStageInfo,
            .layout = m_layout
        };

        VK_CHECK_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE,  1, &createInfo, nullptr, &m_pipeline));
    }

    void bind(VkCommandBuffer commandBuffer, const DescriptorSet& descriptors, uint32_t set = 0) const
    {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, set, 1, descriptors.set(), 0, nullptr);
    }

    template<typename T>
    void pushConstant(VkCommandBuffer commandBuffer, const T& value) 
    {
        assert(m_pushConstantSize == sizeof(T));
        vkCmdPushConstants(commandBuffer, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, m_pushConstantSize, &value);
    }

    void dispatch(VkCommandBuffer commandBuffer, uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) const 
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdDispatch(commandBuffer, groupsX, groupsY, groupsZ);
    }

    void dispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer arguments, VkDeviceSize offset=0) const 
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdDispatchIndirect(commandBuffer, arguments, offset);
    }

    ComputePass(const ComputePass&) = delete;
    ComputePass& operator=(const ComputePass&) = delete;

    ComputePass(ComputePass&& other) noexcept {
        *this = std::move(other);
    }

    ComputePass& operator=(ComputePass&& other) noexcept {
        if (this != &other) {
            m_layout = other.m_layout;
            m_device = other.m_device;
            m_pipeline = other.m_pipeline;
            m_pushConstantSize = other.m_pushConstantSize;

            other.m_layout = VK_NULL_HANDLE;
            other.m_device = VK_NULL_HANDLE;
            other.m_pipeline = VK_NULL_HANDLE;
        }

        return *this;        
    }

    ~ComputePass() {
        if (m_device != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_device, m_layout, nullptr);
            vkDestroyPipeline(m_device, m_pipeline, nullptr);
            m_device = VK_NULL_HANDLE;
        }
    }
};