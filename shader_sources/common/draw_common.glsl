#ifndef ENVIRONMENT_GLSL_
#define ENVIRONMENT_GLSL_

layout(push_constant) uniform EnvironmentUBO {
    mat4 cameraViewProjection;
    vec4 cameraPosition;
    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 envColorIntensity;
} Env;

#endif