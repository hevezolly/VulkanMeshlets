#ifndef CULLING_UTILS_GLSL_
#define CULLING_UTILS_GLSL_

#include "common/constants.glsl"

struct Frustum {
    vec4 planes[6];
};

vec4 transformBoundingSphere(mat4 transform, vec4 boundingSphere) {
    // assume uniform scaling
    float scale = length(transform[0].xyz);

    boundingSphere.xyz = (transform * vec4(boundingSphere.xyz, 1)).xyz;
    boundingSphere.w *= scale;
    return boundingSphere;
}

void extractFrustumPlanes(mat4 proj, out Frustum frustum)
{
    mat4 m = transpose(proj);

    frustum.planes[0] = m[3] + m[0]; // Left
    frustum.planes[1] = m[3] - m[0]; // Right
    frustum.planes[2] = m[3] + m[1]; // Bottom
    frustum.planes[3] = m[3] - m[1]; // Top
    frustum.planes[4] = m[2];        // Near
    frustum.planes[5] = m[3] - m[2]; // Far

    for (int i = 0; i < 6; ++i)
    {
        float len = length(frustum.planes[i].xyz);
        if (len > 1e-6)
            frustum.planes[i] /= len;
    }
}

bool isSphereVisible(vec4 boundingSphere, Frustum frustum)
{
    for (int i = 0; i < 6; ++i)
    {
        float distance = dot(frustum.planes[i].xyz, boundingSphere.xyz)
                       + frustum.planes[i].w;

        if (distance < -boundingSphere.w)
            return false;
    }

    return true;
}

struct CullingCone {
    vec3 apex;
    vec3 axis;
    float cutoff;
};

void transformCullingCone(mat4 transform, inout CullingCone cone) {
    cone.apex = (transform * vec4(cone.apex, 1.)).xyz;
    cone.axis = mat3(transform) * cone.axis;
    cone.axis = normalize(cone.axis);
}

bool isConeFrontFacing(vec3 cameraPosition, CullingCone cone) {

    if (cone.cutoff >= 1.) {
        return true; // meshoptimizer uses >=1 for unusable cones 
    }

    vec3 view = normalize(cameraPosition - cone.apex);

    return dot(view, cone.axis) > -cone.cutoff;
}

bool isMeshletVisible(
    in Meshlet meshlet,
    in mat4 transform,
    in mat4 cameraViewProjection,
    in vec3 cameraPosition
) {
    Frustum frustum;
    extractFrustumPlanes(cameraViewProjection, frustum);
    vec4 boundingSphere = transformBoundingSphere(transform, meshlet.boundingSphere);

    bool meshletVisible = isSphereVisible(boundingSphere, frustum);

    if (meshletVisible) {
        CullingCone cone;
        cone.apex = meshlet.coneApex.xyz;
        cone.axis = meshlet.coneAxisAngle.xyz;
        cone.cutoff = meshlet.coneAxisAngle.w;
        transformCullingCone(transform, cone);
        meshletVisible = isConeFrontFacing(cameraPosition, cone);
    }

    return meshletVisible;
}

#endif