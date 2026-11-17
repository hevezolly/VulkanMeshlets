cmake_minimum_required(VERSION 3.20)
project(VulkanMeshlets LANGUAGES C CXX)

option(CMAKE_SHADERS_O0 "Disable GLSL shader optimization" OFF)

find_package(Vulkan REQUIRED)
add_subdirectory(base)
add_subdirectory(external/meshoptimizer EXCLUDE_FROM_ALL)

target_compile_features(base PUBLIC cxx_std_20)
target_compile_definitions(base
    PRIVATE "VK_EXAMPLE_ASSETS_DIR=\"./assets/\"" "VK_EXAMPLE_SHADERS_DIR=\"./shaders/\"")
if(WIN32)
    target_compile_definitions(base PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX=)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
    target_compile_definitions(base PUBLIC VK_USE_PLATFORM_XCB_KHR)
    target_link_libraries(base PUBLIC PkgConfig::XCB)
endif()
target_include_directories(base
    PUBLIC
        base
        external/imgui
        external/ktx/include
    PRIVATE
        external/meshoptimizer
        external/glm
        external/ktx/other_include
)

add_executable(VulkanMeshlets
    src/main.cpp
    src/scene.cpp
    src/scene.h
)
if(WIN32)
    set_target_properties(VulkanMeshlets PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
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
        "-DCMAKE_SHADERS_O0:BOOL=${CMAKE_SHADERS_O0}"
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
