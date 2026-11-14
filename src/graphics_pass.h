#pragma once
#include <vulkan/vulkan.h>
#include "descriptors.h"
#include "VulkanTools.h"
#include "initializer_list"
#include <array>

class GraphicsPass {
    VkRenderPass m_pass;
    VkPipeline m_pipeline;
    VkPipelineLayout m_layout;
    VkDevice m_device;
public:

    GraphicsPass(): m_device(VK_NULL_HANDLE), m_pass(VK_NULL_HANDLE), m_pipeline(VK_NULL_HANDLE), m_layout(VK_NULL_HANDLE){}

    inline VkPipelineLayout pipelineLayout() const {
        return m_layout;
    }

    GraphicsPass(
        VkDevice device, 
        // create shaders outside to utilize sample's loader functions and decrease boilerplate
        std::initializer_list<VkPipelineShaderStageCreateInfo> shaderStages,  
        VkFormat dsFormat, VkFormat colorFormat,
        std::initializer_list<DescriptorSetLayout> layouts = {}
    ): m_device(device)
    {
        std::vector<VkDescriptorSetLayout> collectedLayouts{};

        for (const auto& l : layouts) {
            collectedLayouts.push_back(*l.layout());
        }

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{ 
			.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = static_cast<uint32_t>(collectedLayouts.size()),
            .pSetLayouts = collectedLayouts.data()
		};

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_layout));

		// Pipeline
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentStateCI = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentStateCI);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		const std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);

        // don't bother with vertex attributes - will read vertices manually in the shader
		VkPipelineVertexInputStateCreateInfo vertexInputStateCI{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 0,
			.pVertexBindingDescriptions = nullptr,
			.vertexAttributeDescriptionCount = 0,
			.pVertexAttributeDescriptions = nullptr,
		};

        std::array<VkAttachmentDescription, 2> attachments{
            // Color attachment
            VkAttachmentDescription{
                .format = colorFormat,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            },
            // Depth attachment
            VkAttachmentDescription{
                .format = dsFormat,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            }
        };

        VkAttachmentReference colorReference{ .attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depthReference{ .attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpassDescription{
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorReference,
            .pDepthStencilAttachment = &depthReference,
        };

        // Subpass dependencies for layout transitions
        std::array<VkSubpassDependency, 2> dependencies{
            VkSubpassDependency{
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
            },
            VkSubpassDependency{
                .srcSubpass = VK_SUBPASS_EXTERNAL,
                .dstSubpass = 0,
                .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
            }
        };

        VkRenderPassCreateInfo renderPassInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .subpassCount = 1,
            .pSubpasses = &subpassDescription,
            .dependencyCount = static_cast<uint32_t>(dependencies.size()),
            .pDependencies = dependencies.data(),
        };
        VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_pass));

		VkGraphicsPipelineCreateInfo pipelineCI{
			.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
			.stageCount = static_cast<uint32_t>(shaderStages.size()),
			.pStages = shaderStages.begin(),
			.pVertexInputState = &vertexInputStateCI,
			.pInputAssemblyState = &inputAssemblyStateCI,
			.pViewportState = &viewportStateCI,
			.pRasterizationState = &rasterizationStateCI,
			.pMultisampleState = &multisampleStateCI,
			.pDepthStencilState = &depthStencilStateCI,
			.pColorBlendState = &colorBlendStateCI,
			.pDynamicState = &dynamicStateCI,
			.layout = m_layout,
			.renderPass = m_pass,
		};

		// Solid rendering pipeline
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, nullptr, 1, &pipelineCI, nullptr, &m_pipeline));
    }

    void beginPass(
        VkCommandBuffer commandBuffer, 
        VkFramebuffer frameBuffer, 
        VkExtent2D targetExtent
    ) 
    {    
        VkClearValue clearValues[2]{};
		clearValues[0].color = { { 0.8f, 0.8f, 0.8f, 1.0f } };;
		clearValues[1].depthStencil = { 1.0f, 0 };
        VkRenderPassBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = m_pass,
            .framebuffer = frameBuffer,
            .renderArea = VkRect2D{
                .offset = {0, 0},
                .extent = targetExtent
            },
            .clearValueCount = 2,
            .pClearValues = clearValues
        };

        vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        VkViewport viewport{
            .x = 0.f,
            .y = 0.f,
            .width = static_cast<float>(targetExtent.width),
            .height = static_cast<float>(targetExtent.height),
            .minDepth = 0.f,
            .maxDepth = 1.f
        };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{
            .offset = {0, 0},
            .extent = targetExtent
        };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    }

    void bind(VkCommandBuffer commandBuffer, const DescriptorSet& descriptor, uint32_t set=0) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, set, 1, descriptor.set(), 0, nullptr);
    }

    void drawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer indexBuffer, VkBuffer drawCommandsBuffer, uint32_t count) {
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VkIndexType::VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(commandBuffer, drawCommandsBuffer, 0, count, sizeof(VkDrawIndexedIndirectCommand));
    }

    // for debugging purposes
    void draw(VkCommandBuffer commandBuffer, uint32_t count) {
        vkCmdDraw(commandBuffer, count, 1, 0, 0);
    }

    void drawIndexed(VkCommandBuffer commandBuffer, VkBuffer indexBuffer, uint32_t count) 
    {    
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VkIndexType::VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, count, 1, 0, 0, 0);
        vkCmdDraw(commandBuffer, count, 1, 0, 0);
    }

    void endPass(VkCommandBuffer commandBuffer) {
        vkCmdEndRenderPass(commandBuffer);
    }

    GraphicsPass(const GraphicsPass&) = delete;
    GraphicsPass& operator=(const GraphicsPass&) = delete;

    GraphicsPass(GraphicsPass&& other) noexcept {
        *this = std::move(other);
    }

    GraphicsPass& operator=(GraphicsPass&& other) noexcept {
        if (this != &other) {
            m_layout = other.m_layout;
            m_device = other.m_device;
            m_pipeline = other.m_pipeline;
            m_pass = other.m_pass;

            other.m_layout = VK_NULL_HANDLE;
            other.m_device = VK_NULL_HANDLE;
            other.m_pipeline = VK_NULL_HANDLE;
            other.m_pass = VK_NULL_HANDLE;
        }

        return *this;        
    }

    ~GraphicsPass() {
        if (m_device != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, m_pipeline, nullptr);
            vkDestroyPipelineLayout(m_device, m_layout, nullptr);
            vkDestroyRenderPass(m_device, m_pass, nullptr);
        }
    }
};
