#ifndef MULTIDRAW_BUFFERS_GLSL_
#define MULTIDRAW_BUFFERS_GLSL_

#ifndef MULTIDRAW_BUFFERS_SET
#define MULTIDRAW_BUFFERS_SET 3
#endif

#include "common/constants.glsl"

layout(set = MULTIDRAW_BUFFERS_SET, binding = 0, std430) buffer DrawCommandsBinding {
    VkDrawIndexedIndirectCommand DrawIndirectCommands[];
};

#endif