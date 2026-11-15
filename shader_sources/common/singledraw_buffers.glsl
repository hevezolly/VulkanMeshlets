#ifndef SINGLEDRAW_BUFFERS_GLSL_
#define SINGLEDRAW_BUFFERS_GLSL_

#ifndef SINGLEDRAW_ACCESS
#define SINGLEDRAW_ACCESS
#endif

#ifndef SINGLEDRAW_BUFFERS_SET
#define SINGLEDRAW_BUFFERS_SET 3
#endif

#include "common/constants.glsl"

layout(set = SINGLEDRAW_BUFFERS_SET, binding = 0, std430) SINGLEDRAW_ACCESS buffer PerMeshletWorkloadBinding {
    PerMeshletWorkload PerMeshletWorkloads[];
};

layout(set = SINGLEDRAW_BUFFERS_SET, binding = 1, std430) SINGLEDRAW_ACCESS buffer CulledVertexBufferBinding {
    uvec4 CulledVertices[];
};

layout(set = SINGLEDRAW_BUFFERS_SET, binding = 2, std430) SINGLEDRAW_ACCESS buffer CulledIndexBufferBinding {
    uint CulledIndices[];
};

#endif