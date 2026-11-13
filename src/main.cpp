#include "vulkanexamplebase.h"
#include "synchronization.h"
#include "descriptors.h"
#include "graphics_pass.h"
#include "scene.h"

struct VulkanExample final : public VulkanExampleBase
{
    uint32_t currentFrame = 0;
    Synchronization sync{};
    DescriptorPool descriptors{};
    GraphicsPass graphicsPass{};
    
    VulkanExample()
    {
        title = "Vulkan Meshlets";
        settings.overlay = false;
    }

    void prepare() override 
    {
        VulkanExampleBase::prepare();
        sync.prepare(device, maxConcurrentFrames, swapChain.imageCount);

        graphicsPass.prepare(device, {
            loadShader(getShadersPath() + "temp.frag", VK_SHADER_STAGE_FRAGMENT_BIT),
            loadShader(getShadersPath() + "temp.vert", VK_SHADER_STAGE_VERTEX_BIT),
        }, depthFormat, swapChain.colorFormat);

        // add layouts
        descriptors.prepare(device);

        prepared = true;
    }

    void render() override 
    {
        if (!prepared)
            return;

        vkWaitForFences(device, 1, &sync.waitFences[currentFrame], VK_TRUE, UINT64_MAX);
        VK_CHECK_RESULT(vkResetFences(device, 1, &sync.waitFences[currentFrame]));

        uint32_t imageIndex;
		VkResult result = vkAcquireNextImageKHR(device, swapChain.swapChain, UINT64_MAX, sync.presentCompleteSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR) {
			windowResize();
			return;
		}
		else if ((result != VK_SUCCESS) && (result != VK_SUBOPTIMAL_KHR)) {
			throw "Could not acquire the next swap chain image!";
		}

        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };

        // reuse drawCmdBuffers and frameBuffers from VulkanExampleBase - nothing particularly interesting to do with them
        VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[currentFrame], &beginInfo));
        {
            graphicsPass.beginPass(drawCmdBuffers[currentFrame], frameBuffers[imageIndex], {width, height});
            graphicsPass.draw(drawCmdBuffers[currentFrame], 3);
            graphicsPass.endPass(drawCmdBuffers[currentFrame]);
        }
        VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[currentFrame]));

        const VkPipelineStageFlags waitPipelineStage{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSubmitInfo submitInfo{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &sync.presentCompleteSemaphores[currentFrame],
			.pWaitDstStageMask = &waitPipelineStage,
			.commandBufferCount = 1,
			.pCommandBuffers = &drawCmdBuffers[currentFrame],
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &sync.renderToSwapchainImgCompleteSemaphores[imageIndex]
		};
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, sync.waitFences[currentFrame]));

        VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &sync.renderToSwapchainImgCompleteSemaphores[imageIndex],
            .swapchainCount = 1,
            .pSwapchains = &swapChain.swapChain,
            .pImageIndices = &imageIndex
        };
		result = vkQueuePresentKHR(queue, &presentInfo);

		if ((result == VK_ERROR_OUT_OF_DATE_KHR) || (result == VK_SUBOPTIMAL_KHR)) {
			windowResize();
		}
		else if (result != VK_SUCCESS) {
			throw "Could not present the image to the swap chain!";
		}

        currentFrame = (currentFrame + 1) % maxConcurrentFrames;
    }
};

VULKAN_EXAMPLE_MAIN()
