#version 450
#extension GL_GOOGLE_include_directive : require

#include "common/draw_common.glsl"

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inPosition;

void main() {

    vec3 v = normalize(Env.cameraPosition.xyz - inPosition);
    vec3 n = normalize(inNormal);

    vec3 dirLambertian = dot(n, normalize(Env.lightDirIntensity.xyz)) * Env.lightColor.xyz * inColor * Env.lightDirIntensity.w;
    vec3 ambient = Env.envColorIntensity.xyz * Env.envColorIntensity.w;

    outColor = vec4(dirLambertian + ambient, 1);
}