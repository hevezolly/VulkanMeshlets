# VulkanMeshlets

Solution vulkan meshlets assignment.

### Features

- glTF mesh loading
- CPU meshlet generation via [meshoptimizer lib](https://github.com/zeux/meshoptimizer)
- model instancing
- per-meshlet frustum and cone culling
- rendering via vkCmdDrawIndexedIndirect with both multiDrawIndirect and without it (using a single command)
- simple Lambertian shading
- Windows and Linux build support
- required buffer sizes read-back and ammortization
- build on top of [Sascha Willems Vulkan example](https://github.com/SaschaWillems/Vulkan/blob/master/examples/gltfloading/gltfloading.cpp)

### Build
The project is built using CMake.
It requires either VulkanSDK installed, or vulkan lib and headers should be provided manually
#### Windows
from the project root:
```
cmake -S . -B build
// debug build:
cmake --build build --config Debug
// release build:
cmake --build build --config Release
```
if vulkan SDK is not installed, path to vulkan headers and lib should be provided as follows:
```
cmake -S . -B build -DVulkan_INCLUDE_DIR=<include-directory> -DVulkan_LIBRARY=<path-to-vulkan-1.lib>
```
#### Linux
from the project root:
from the project root:
```
// debug build:
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug

// release build:
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
```
if vulkan SDK is not installed, `libvulkan-dev` can be installed instead
#### Shaders
Shaders are compiled from glsl to spir-v with `glslc`. If it is present - the shaders from `shaders_sources` folder would be built and copied to `shaders/glsl` before every build.

If `glslc` can't be found no shaders would be rebuilt - `shaders/glsl` would be used as is (it already contains built spir-v binaries).

### Usage

startup argument `--single-draw` can be used to force disable multidraw branch.
startup argument `--instances n` can be used to controll the number of model instances (4000 by default)

`WASD` - to control camera forwadr, backward and side-to-side movement. `ctrl` - down, `space` - up. Camera rotation is performed by mouse, when right mouse button is held down.

There is an option in the UI to freeze culling frustum, to be able to see the results of culling.

### GPU algorithms overview

1. Command counters, commands and buffers containing indirect commands are cleared
2. Compute work is launched per instance. Each thread frustum-culls its corresponding instance, as well as generates a number of workloads, containing a meshlet range with meshlet to instance correspondance.
   Each workload can have up to `MeshletsProcessedPerGroup = 256` meshlets. Used number of workloads is read back to cpu to estimate optimal buffer size.
3. For when the multi-draw is enabled:
    1. Indirect dispatch is performed with `local_size_x = MeshletsProcessedPerGroup` and number of groups X eqal to the number of workloads. The shader culls individual meshlets using frustum and view cone.
       After that it generates a number of indirect draw commands. Neighbouring visible meshlets within the work group are combined into a single indirect command with the use of shared memory and barriers.
       It is possible due to specific vertex and index buffers arrangement - meshlets are occupying contigious regions of the index buffer.
       Instance id is passed to the vertex shader via `firstInstance` field of the indirect command. Used number of commands is read back to cpu to estimate both optimal buffer size and command count for indirect draw.
    2. vkCmdDrawIndexedIndirect is executed. Command count is estimated from previous frames. Unused commands are zeroed-out and don't produce any draw instructions.
       Vertex shader uses `gl_VertexIndex` to fetch vertex position and normal, and `gl_InstanceIndex` to fetch instance transform and color.
4. For when the multi-draw is disabled:
    1. Indirect dispatch is performed, similar to the multi-draw path. Meshlets are frustum and cone culled.
       For every visible meshlet the shader appends to a buffer the structure, containing meshlet id and instance id. Single indirect draw's command instance count is incremented as well.
       Desired number of instances is read back to cpu to estimate optimal buffer size.
    2. vkCmdDrawIndexedIndirect is executed with the single indirect command. It uses degenerate index buffer, containing sequential indices from 0 to max number of indices in a meshlet.
       The vertex shader reads meshlet id and instance id from the generated buffer at `gl_InstanceIndex`, reads index offset from the meshlet using meshlet id, and calculates final vertex index via
       `meshlet index offset + gl_VertexIndex`. In case the meshlet uses less indices then the maximum - unused vertices land at 0 and don't produce visible triangles.

#### Performace considerations

Both execution modes have similar asymptotic time and memory complexity - number of final draw instances/commands is proportional to the number of visible meshlets.

However there are advantages of multiDraw approach and disadvantages of single-draw:
1. multi-draw makes it possible to batch multiple visible meshlets into a single command. On a scene with ~1M visible meshlets this reduced the nuber commands in ~13 times compared to a naive approach of one command per meshlet. It also allows to reduce memory requirements for indirect commands buffer.
2. single-draw puts more work on the vertex shader: vertex fetch is more complex than the one in multi-draw case. Also VS often have to process redundant vertices.

Due to those reasons, it takes ~1.1ms more to render 1M visible meshlets in single-draw mode on my machine. 

#### Note
Simpler algorithm is possible, but intentionally avoided: It is possible to take advantage of a fact that every instance has the same amount of meshlets. Compute work can be executed over `instance count * meshlet count` from which both meshlet id and instance id could be aquired using modulo. This way step 2. could've been skipped. However this method breaks as soon as different models are added. Since it is unusual for a real-world engine to use only one type of model (or for each model to have the same amount of meshlets) more complex algorithm was implemented, that supports multiple model types.

### CPU side overview
The project relies havily on [Sascha Willems Vulkan example](https://github.com/SaschaWillems/Vulkan/blob/master/examples/gltfloading/gltfloading.cpp). Cross-frame synchronization, device, swapchain, depth target, frame buffer and command lists and resource creation is borrowed from it (there is nothing particularly interesting to do with them in the scope of the task - most of them would've been just the most basic implementation). Ui and input are also used from the examples.

My own code is located in `src`, `cmake` and `shader_sources`. `base` and `external` are third-party.

Parts that were implemented on CPU side and worth mentioning: 
- simple descriptor sets and layouts management
- compute and graphics passes cration and usage
- helpers to decrease barriers boilerplate
- simple controller for ammortizing buffer capacities and command counts, received from GPU

### Things to add/improve
There are things that were not implemented or iterated on due to the scope of the project. Most notible are:
- better resource management and GPU memory reuse:
  The project shrinks and grows gpu buffers quite often throughout the run. In the current version it is done by simply creating new buffers. Some form of pooling or memory reuse could be usefull.
- capacity ammortization policy can be improved:
  The project reads data from GPU to CPU in order to estimate required buffer sizes and command count. The algorithm for it is quite simple. It can lead to geometry flashing breifly on sudden camera movements.
  Some better predition algorithm would be needed.
- cpu side memory management:
  The project doesn't use any custom allocators (pooling or arena) for transient memory. Instead `std::vector` is often used, which causes unnecessary heap allocations.
  Adding allocators with less overhead can also prove usefull.

### Example
full version: https://youtu.be/X36Q5upHMVA

preview:

https://github.com/user-attachments/assets/1255eac5-6d7d-4e74-834d-64212dc8672f



