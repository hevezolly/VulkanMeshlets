cmake_minimum_required(VERSION 3.20)
project(VulkanMeshlets LANGUAGES C CXX)

find_package(Vulkan REQUIRED)
add_subdirectory(base)

target_compile_features(base PUBLIC cxx_std_20)
target_compile_definitions(base PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX=)
target_include_directories(base
    PUBLIC
        base
        external/glm
        external/imgui
        external/ktx/include
        ${Vulkan_INCLUDE_DIRS}
    PRIVATE
        external/ktx/other_include
)

add_executable(VulkanMeshlets WIN32 main.cpp)
target_link_libraries(VulkanMeshlets PRIVATE base)
