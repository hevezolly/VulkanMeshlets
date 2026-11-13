#pragma once
#include <vulkan/vulkan.h>
#include "descriptors.h"
#include "VulkanTools.h"

class ComputePass {
    VkPipeline m_pipeline;
    VkPipelineLayout m_layout;
    VkDevice m_device;

public:

    ComputePass(): m_device(VK_NULL_HANDLE), m_pipeline(VK_NULL_HANDLE), m_layout(VK_NULL_HANDLE) {}

    void prepare(VkDevice device, VkShaderModule shaderModule, const DescriptorSetLayout& layout) {
        m_device = device;
        VkPipelineLayoutCreateInfo layoutInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = layout.layout()
        };

        VK_CHECK_RESULT(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_layout));

        VkPipelineShaderStageCreateInfo shaderStageInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main"
        };
        
        VkComputePipelineCreateInfo createInfo {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = shaderStageInfo,
            .layout = m_layout
        };

        VK_CHECK_RESULT(vkCreateComputePipelines(device, VK_NULL_HANDLE,  1, &createInfo, nullptr, &m_pipeline));
    }

    void dispatch(VkCommandBuffer commandBuffer, uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) const 
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdDispatch(commandBuffer, groupsX, groupsY, groupsZ);
    }

    void dispatch(
        VkCommandBuffer commandBuffer, 
        const DescriptorSet& descriptors, 
        uint32_t groupsX, uint32_t groupsY = 1, uint32_t groupsZ = 1) const 
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, descriptors.set(), 0, nullptr); 
        vkCmdDispatch(commandBuffer, groupsX, groupsY, groupsZ);
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