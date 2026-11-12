#include "vulkanexamplebase.h"


class VulkanExample final : public VulkanExampleBase
{
public:
    VulkanExample()
    {
        title = "Vulkan Meshlets";
        settings.overlay = false;
    }

private:
    void render() override {}
};

VULKAN_EXAMPLE_MAIN()
