#pragma once
#include <vector>
#include "VulkanTools.h"
#include <assert.h>
#include <vulkan/vulkan.h>

struct Synchronization {
    std::vector<VkFence> waitFences{};
    std::vector<VkSemaphore> presentCompleteSemaphores{};
	std::vector<VkSemaphore> renderToSwapchainImgCompleteSemaphores{};

    Synchronization(): waitFences(), presentCompleteSemaphores(), renderToSwapchainImgCompleteSemaphores(){}
    Synchronization(const Synchronization&) = delete;
    Synchronization& operator=(const Synchronization&) = delete;
    Synchronization(Synchronization&& other) = delete;
    Synchronization& operator=(Synchronization&& other) = delete;

    ~Synchronization() {
        if (m_device != VK_NULL_HANDLE) {

            for (size_t i = 0; i < presentCompleteSemaphores.size(); i++) {
                vkDestroySemaphore(m_device, presentCompleteSemaphores[i], nullptr);
            }

            for (size_t i = 0; i < renderToSwapchainImgCompleteSemaphores.size(); i++) {
                vkDestroySemaphore(m_device, renderToSwapchainImgCompleteSemaphores[i], nullptr);
            }

            for (size_t i = 0; i < waitFences.size(); i++) {
                vkDestroyFence(m_device, waitFences[i], nullptr);
            }


            m_device = VK_NULL_HANDLE;
        }
    }

    void prepare(VkDevice device, uint32_t framesInFlight, uint32_t swapchainImages) 
    {
        assert(m_device == VK_NULL_HANDLE);

        m_device = device;    
        waitFences.resize(framesInFlight);
        presentCompleteSemaphores.resize(framesInFlight);
        renderToSwapchainImgCompleteSemaphores.resize(swapchainImages);

        for (uint32_t i = 0; i < framesInFlight; i++) {
            VkFenceCreateInfo fenceInfo {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT
            };
            VK_CHECK_RESULT(vkCreateFence(device, &fenceInfo, nullptr, &waitFences[i]));

            VkSemaphoreCreateInfo semaphoreInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
            };
            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &presentCompleteSemaphores[i]));
        }

        for (uint32_t i = 0; i < swapchainImages; i++) {
            VkSemaphoreCreateInfo semaphoreInfo {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
            };
            VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderToSwapchainImgCompleteSemaphores[i]));
        }
    }
private: 
    VkDevice m_device = VK_NULL_HANDLE;
};
