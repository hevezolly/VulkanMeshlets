#include "vulkanexamplebase.h"
#include "descriptors.h"
#include "graphics_pass.h"
#include "scene.h"

struct VulkanExample final : public VulkanExampleBase
{
    DescriptorPool descriptors{};
    GraphicsPass graphicsPass{};
    Scene scene{};
    vks::Buffer vertexBuffer, indexBuffer;
    std::optional<DescriptorSet> modelDescriptors;

    VulkanExample()
    {
        title = "Vulkan Meshlets";
        settings.overlay = true;
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

        resized = false;
        prepareFrame();
        if (resized) {
            return;
        }

        VkCommandBufferBeginInfo beginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        };

        // reuse drawCmdBuffers and frameBuffers from VulkanExampleBase - nothing particularly interesting to do with them
        auto commandBuffer = drawCmdBuffers[currentBuffer];
        VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        {
            auto proj = glm::perspective(
                glm::radians(45.0f), width / (float) height, 0.1f, 100.0f);
            proj[1][1] *= -1;
            
            glm::mat4 viewProj =proj *
                glm::lookAt(glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

            graphicsPass.beginPass(commandBuffer, frameBuffers[currentImageIndex], {width, height});
            vkCmdPushConstants(commandBuffer, graphicsPass.pipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &viewProj);
            graphicsPass.bind(commandBuffer, *modelDescriptors);
            graphicsPass.drawIndexed(commandBuffer, indexBuffer.buffer, scene.indices().size());

            VulkanExampleBase::drawUI(commandBuffer);

            graphicsPass.endPass(commandBuffer);
        }
        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        submitFrame();
    }
};

VULKAN_EXAMPLE_MAIN()
