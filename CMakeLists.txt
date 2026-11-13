cmake_minimum_required(VERSION 3.20)
project(VulkanMeshlets LANGUAGES CXX)

find_package(Vulkan REQUIRED)
add_subdirectory(base)
add_subdirectory(external/meshoptimizer EXCLUDE_FROM_ALL)

target_compile_features(base PUBLIC cxx_std_20)
target_compile_definitions(base
    PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX=
    PRIVATE "VK_EXAMPLE_ASSETS_DIR=\"./assets/\"" "VK_EXAMPLE_SHADERS_DIR=\"./shaders/\"")
target_include_directories(base
    PUBLIC
        base
        external/imgui
        external/ktx/include
        ${Vulkan_INCLUDE_DIRS}
    PRIVATE
        external/meshoptimizer
        external/glm
        external/ktx/other_include
)

add_executable(VulkanMeshlets WIN32
    src/main.cpp
    src/scene.cpp
    src/scene.h
)
target_include_directories(VulkanMeshlets PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/base"
    external/tinygltf
    external/glm
)
target_link_libraries(VulkanMeshlets PRIVATE base meshoptimizer)

add_custom_target(VulkanMeshletsShaders
    COMMAND "${CMAKE_COMMAND}"
        "-DSHADER_PROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DSHADER_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompileShaders.cmake"
    COMMENT "Preparing GLSL shaders"
    VERBATIM
)
add_custom_target(VulkanMeshletsResources
    COMMAND "${CMAKE_COMMAND}"
        "-DRESOURCE_PROJECT_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DRESOURCE_OUTPUT_DIR=$<TARGET_FILE_DIR:VulkanMeshlets>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CopyResources.cmake"
    COMMENT "Copying assets and shaders beside VulkanMeshlets"
    VERBATIM
)
add_dependencies(VulkanMeshletsResources VulkanMeshletsShaders)
add_dependencies(VulkanMeshlets VulkanMeshletsResources)
