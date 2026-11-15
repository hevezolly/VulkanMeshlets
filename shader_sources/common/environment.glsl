#ifndef ENVIRONMENT_GLSL_
#define ENVIRONMENT_GLSL_

#ifndef ENVIRONMENT_SET
#define ENVIRONMENT_SET 0
#endif

layout(set = ENVIRONMENT_SET, binding = 0) uniform EnvironmentUBO {
    mat4 cameraViewProjection;

    vec4 lightDirIntensity;
    vec4 lightColor;
    vec4 envColorIntensity;
    
    // align to fit into cache lines nicely
    vec4 _padding;
} Env;

#endif