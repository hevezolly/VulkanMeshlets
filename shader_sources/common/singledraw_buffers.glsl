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

#endif