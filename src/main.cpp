#include "vulkanexamplebase.h"
#include "descriptors.h"
#include "graphics_pass.h"
#include "scene.h"
#include "draw.h"
#include "shader_loader.h"
#include <random>
#include <glm/gtx/color_space.hpp>
#include "camera_view_control.h"

constexpr const char* SINGLE_DRAW_ARG_NAME = "singleDraw";
constexpr const char* SINGLE_DRAW_ARG = "--single-draw";

constexpr const char* INSTANCE_COUNT_ARG_NAME = "numberInstances";
constexpr const char* INSTANCE_COUNT_ARG = "--instances";

struct VulkanExample final : public VulkanExampleBase
{
    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
    };

    DescriptorPool descriptors{};
    Scene scene{128, 256};
    GpuScene gpuScene{};
    DrawSystem drawSystem{};
    ShaderLoader shaderLoader;
    uint64_t frameIndex{0};
    CameraViewControl camera;

    VulkanExample(): shaderLoader(this)
    {
        title = "Vulkan Meshlets";
        apiVersion = VK_API_VERSION_1_3;
        settings.overlay = true;

        commandLineParser.add(SINGLE_DRAW_ARG_NAME, {SINGLE_DRAW_ARG}, false, "Force the single-draw fallback");
        commandLineParser.add(INSTANCE_COUNT_ARG_NAME, {INSTANCE_COUNT_ARG}, true, "Number of instances added to the scene");
        commandLineParser.parse(args);
    }

    ~VulkanExample() 
    {
        gpuScene.destroy();
        drawSystem.destroy();
    }

    inline std::string shadersPath(const std::string& name) 
    {
        return getShadersPath() + name;
    }

    void getEnabledFeatures() override
    {
        bool multidrawSupported = deviceFeatures.multiDrawIndirect == VK_TRUE;
        bool drawFirstInstanceSupported = deviceFeatures.drawIndirectFirstInstance == VK_TRUE;
        // drawIndirectFirstInstance is used by multidraw path - disable multidraw if it is not supported
        enabledFeatures.multiDrawIndirect = (multidrawSupported && drawFirstInstanceSupported) ? VK_TRUE : VK_FALSE;
        enabledFeatures.drawIndirectFirstInstance = deviceFeatures.drawIndirectFirstInstance;

        // want to use 1.3 for synchronization2
        if (deviceProperties.apiVersion < VK_API_VERSION_1_3) {
            throw std::runtime_error("The selected GPU does not support Vulkan 1.3");
        }

        VkPhysicalDeviceVulkan13Features supported13{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES
        };
        VkPhysicalDeviceFeatures2 supported{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &supported13
        };

        vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);

        if (!supported13.synchronization2) {
            throw std::runtime_error("The selected GPU does not support synchronization2");
        }

        features13.synchronization2 = VK_TRUE;
        features13.pNext = deviceCreatepNextChain;
        deviceCreatepNextChain = &features13;
    }

    void populateScene() {
        uint32_t bunnyModel = scene.addModel(getAssetPath() + "bunny.gltf");

        std::mt19937 rng(0u);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        const float positionMin = -10;
        const float positionMax = 10;
        const float scaleMin = 0.5;
        const float scaleMax = 2.;

        int32_t instanceCount = commandLineParser.getValueAsInt(INSTANCE_COUNT_ARG_NAME, 4000);

        for (uint32_t i = 0; i < instanceCount; i++) {

            float x = dist(rng) * (positionMax - positionMin) + positionMin;
            float z = dist(rng) * (positionMax - positionMin) + positionMin;

            float scale = dist(rng) * (scaleMax - scaleMin) + scaleMin;
            
            float color = dist(rng) * 360;

            glm::vec3 hsv(color, 1.0f, 0.8f);
            glm::vec3 rgb = glm::rgbColor(hsv);

            float angle = glm::radians(dist(rng) * 360);
            
            scene.instantiate(bunnyModel, glm::vec3(x, 0., z), glm::angleAxis(angle, glm::vec3(0., 1., 0.)), scale, glm::rgbColor(hsv));
        }
    }

    void uploadScene() {
        VkDeviceSize indexBufferSize = scene.indices().size() * sizeof(uint32_t);
        VkDeviceSize vertexBufferSize = scene.vertices().size() * sizeof(Vertex);

        VkDeviceSize modelsBufferSize = scene.models().size() * sizeof(ModelData);
        VkDeviceSize meshletsBufferSize = scene.meshlets().size() * sizeof(Meshlet);
        VkDeviceSize instancesBufferSize = scene.instances().size() * sizeof(InstanceData);
        VkDeviceSize meshletIndicesBufferSize = scene.maxIndexPerMeshlet() * sizeof(uint32_t);

        GpuScene stagingScene;

        std::vector<uint32_t> meshletIndices;
        meshletIndices.reserve(scene.maxIndexPerMeshlet());

        for (uint32_t i = 0; i < scene.maxIndexPerMeshlet(); i++) {
            meshletIndices.push_back(i);
        }

        // staging
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &stagingScene.indexBuffer,
            indexBufferSize,
            scene.indices().data()
        ));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingScene.vertexBuffer,
			vertexBufferSize,
			scene.vertices().data()
        ));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingScene.modelsBuffer,
			modelsBufferSize,
			scene.models().data()
        ));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingScene.meshletsBuffer,
			meshletsBufferSize,
			scene.meshlets().data()
        ));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingScene.instancesBuffer,
			instancesBufferSize,
			scene.instances().data()
        ));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingScene.meshletIndexBuffer,
			meshletIndicesBufferSize,
			meshletIndices.data()
        ));

        // gpu-local
        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            &gpuScene.indexBuffer,
            indexBufferSize
        ));

		VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&gpuScene.vertexBuffer,
			vertexBufferSize
        ));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&gpuScene.modelsBuffer,
			modelsBufferSize
        ));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&gpuScene.meshletsBuffer,
			meshletsBufferSize
        ));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&gpuScene.instancesBuffer,
			instancesBufferSize
        ));

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			&gpuScene.meshletIndexBuffer,
			meshletIndicesBufferSize
        ));

        SceneStats stats {
            .countModels = static_cast<uint32_t>(scene.models().size()),
            .countInstances = static_cast<uint32_t>(scene.instances().size()),
            .countMeshlets = static_cast<uint32_t>(scene.meshlets().size()),
            .maxMeshletIndices = static_cast<uint32_t>(scene.maxIndexPerMeshlet())
        };

        VK_CHECK_RESULT(vulkanDevice->createBuffer(
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, 
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &gpuScene.sceneStats,
            sizeof(SceneStats),
            &stats
        ));

		// Copy data from staging buffers (host) do device local buffer (gpu)
		VkCommandBuffer copyCmd = vulkanDevice->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkBufferCopy copyRegion = {};

		copyRegion.size = indexBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			stagingScene.indexBuffer.buffer,
			gpuScene.indexBuffer.buffer,
			1,
			&copyRegion);

		copyRegion.size = vertexBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			stagingScene.vertexBuffer.buffer,
			gpuScene.vertexBuffer.buffer,
			1,
			&copyRegion);

        copyRegion.size = modelsBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			stagingScene.modelsBuffer.buffer,
			gpuScene.modelsBuffer.buffer,
			1,
			&copyRegion);

        copyRegion.size = meshletsBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			stagingScene.meshletsBuffer.buffer,
			gpuScene.meshletsBuffer.buffer,
			1,
			&copyRegion);

        copyRegion.size = instancesBufferSize;
		vkCmdCopyBuffer(
			copyCmd,
			stagingScene.instancesBuffer.buffer,
			gpuScene.instancesBuffer.buffer,
			1,
			&copyRegion);

        copyRegion.size = meshletIndicesBufferSize;
        vkCmdCopyBuffer(
			copyCmd,
			stagingScene.meshletIndexBuffer.buffer,
			gpuScene.meshletIndexBuffer.buffer,
			1,
			&copyRegion);

		vulkanDevice->flushCommandBuffer(copyCmd, queue, true);

		stagingScene.destroy();
    }

    void prepare() override 
    {
        VulkanExampleBase::prepare();
        
        populateScene();
        uploadScene();
        bool useMultiDraw = enabledFeatures.multiDrawIndirect == VK_TRUE &&
            !commandLineParser.isSet(SINGLE_DRAW_ARG_NAME);
        drawSystem.registerLayouts(device, maxConcurrentFrames, descriptors, useMultiDraw);

        descriptors.prepare(device);

        drawSystem.prepare(
            vulkanDevice, 
            descriptors, 
            gpuScene, 
            scene, 
            shaderLoader, 
            depthFormat, 
            swapChain.colorFormat,
            deviceProperties.limits
        );

        prepared = true;
    }

    void OnUpdateUIOverlay(vks::UIOverlay* overlay) override
    {
        if (overlay->header("scene stats")) {
            overlay->text("model count:    %zu", scene.models().size());
            overlay->text("instance count: %zu", scene.instances().size());
            overlay->text("meshlet count:  %zu", scene.meshlets().size());
            overlay->text("vertices count: %zu", scene.vertices().size());
            overlay->text("indices count:  %zu", scene.indices().size());
        }

        drawSystem.updateUI(*overlay);
    }

    void keyStateChanged(uint32_t key, bool pressed) override
    {
        using CameraKey = CameraViewControl::CameraKey;
        switch (key) {
        case KEY_W: camera.setCameraKey(CameraKey::Forward, pressed); break;
        case KEY_S: camera.setCameraKey(CameraKey::Backward, pressed); break;
        case KEY_A: camera.setCameraKey(CameraKey::Left, pressed); break;
        case KEY_D: camera.setCameraKey(CameraKey::Right, pressed); break;
        case KEY_SPACE: camera.setCameraKey(CameraKey::Up, pressed); break;
#if defined(_WIN32)
        case VK_CONTROL:
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
        case KEY_LEFTCTRL:
        case KEY_RIGHTCTRL:
#elif defined(VK_USE_PLATFORM_XCB_KHR)
        case 0x25: // Left Control in the default X11 keymap.
        case 0x69: // Right Control in the default X11 keymap.
#endif
#if defined(_WIN32) || defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_XCB_KHR)
            camera.setCameraKey(CameraKey::Down, pressed);
            break;
#endif
        default:
            break;
        }
    }

    void inputFocusLost() override
    {
        camera.clearCameraKeys();
    }

    void mouseMoved(double x, double y, bool& handled) override
    {
        if (handled || !mouseState.buttons.right) {
            return;
        }
        camera.rotateCamera(static_cast<float>(x) - mouseState.position.x,
            static_cast<float>(y) - mouseState.position.y);
        handled = true;
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
            camera.updateCamera(frameTimer);
            drawSystem.beginDraw(
                commandBuffer, 
                currentBuffer, 
                frameIndex, 
                frameBuffers[currentImageIndex], 
                {width, height}, 
                scene, 
                gpuScene,
                camera.calculateView()
            );
            VulkanExampleBase::drawUI(commandBuffer);
            drawSystem.endDraw(commandBuffer);
        }
        VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

        submitFrame();
        frameIndex++;
    }
};

VkPipelineShaderStageCreateInfo ShaderLoader::loadShader(const std::string& path, VkShaderStageFlagBits stage) const {
    return m_baseLoader->loadShader(m_baseLoader->shadersPath(path), stage);
}

VULKAN_EXAMPLE_MAIN()
