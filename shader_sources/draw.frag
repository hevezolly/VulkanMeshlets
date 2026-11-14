#version 450
#extension GL_GOOGLE_include_directive : require

#include "common/environment.glsl"

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;

void main() {
    outColor = vec4(inColor, 1.0) * normalize(inNormal).y;
}