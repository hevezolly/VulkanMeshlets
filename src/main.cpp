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
    Scene scene{};
    vks::Buffer vertexBuffer, indexBuffer;
    std::optional<DescriptorSet> modelDescriptors;

    VulkanExample()
    {
        title = "Vulkan Meshlets";
        settings.overlay = false;
    }

    ~VulkanExample() 
    {
        vertexBuffer.destroy();
        indexBuffer.destroy();
    }

    void populateScene() {
        uint32_t bunnyModel = scene.addModel(getAssetPath() + "bunny.gltf");
    }

    void uploadScene(const DescriptorSetLayout& modelLayout) {
        vks::Buffer vertexStaging, indexStaging;

        VkDeviceSize vertexBufferSize = scene.vertices().size() * sizeof(Vertex);
        VkDeviceSize indexBufferSize = scene.indices().size() * sizeof(uint32_t);

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &vertexStaging,
            vertexBufferSize,
            scene.vertices().data()
        ));
		// Index data
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&indexStaging,
			indexBufferSize,
			scene.indices().data()
        ));

		// Create device local buffers (target)
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&vertexBuffer,
			vertexBufferSize));
		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&indexBuffer,
			indexBufferSize));

		// Copy data from staging buffers (host) do device local buffer (gpu)
		VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy copyRegion = {};

		copyRegion.size = vertexBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			vertexStaging.buffer,
			vertexBuffer.buffer,
			1,
			&copyRegion);

		copyRegion.size = indexBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			indexStaging.buffer,
			indexBuffer.buffer,
			1,
			&copyRegion);

		vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

		vertexStaging.destroy();
		indexStaging.destroy();

        modelDescriptors = descriptors.allocate(modelLayout);
        modelDescriptors->write({
            ResourceHandle::makeBuffer(vertexBuffer.buffer)
        });
    }

    void prepare() override 
    {
        VulkanExampleBase::prepare();
        sync.prepare(device, maxConcurrentFrames, swapChain.imageCount);
        const DescriptorSetLayout modelLayout{
            device,
            {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT}}
        };
        descriptors.registerLayout(modelLayout);
        descriptors.prepare(device);

        populateScene();
        uploadScene(modelLayout);


        graphicsPass.prepare(device, {
            loadShader(getShadersPath() + "temp.frag", VK_SHADER_STAGE_FRAGMENT_BIT),
            loadShader(getShadersPath() + "temp.vert", VK_SHADER_STAGE_VERTEX_BIT),
        }, depthFormat, swapChain.colorFormat, &modelLayout);

        

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
        auto commandBuffer = drawCmdBuffers[currentFrame];
        VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        {
            auto proj = glm::perspective(
                glm::radians(45.0f), width / (float) height, 0.1f, 100.0f);
            proj[1][1] *= -1;
            
            glm::mat4 viewProj =proj *
                glm::lookAt(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

            graphicsPass.beginPass(commandBuffer, frameBuffers[imageIndex], {width, height});
            vkCmdPushConstants(commandBuffer, graphicsPass.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &viewProj);
            graphicsPass.bind(commandBuffer, *modelDescriptors);
            graphicsPass.drawIndexed(commandBuffer, indexBuffer.buffer, scene.indices().size());
            graphicsPass.endPass(commandBuffer);
        }
        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        const VkPipelineStageFlags waitPipelineStage{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		VkSubmitInfo submitInfo{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &sync.presentCompleteSemaphores[currentFrame],
			.pWaitDstStageMask = &waitPipelineStage,
			.commandBufferCount = 1,
			.pCommandBuffers = &commandBuffer,
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
