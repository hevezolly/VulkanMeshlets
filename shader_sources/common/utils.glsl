#ifndef UTILS_GLSL_
#define UTILS_GLSL_

uint hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

vec3 hasvToRgb(vec3 hsv)
{
    vec3 rgb = clamp(
        abs(mod(hsv.x * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0,
        0.0, 1.0
    );

    return hsv.z * mix(vec3(1.0), rgb, hsv.y);
}

vec3 randomColor(uint seed)
{
    float h = float(hash(seed)) * (1.0 / 4294967296.0);
    float s = 0.6 + 0.4 * float(hash(seed + 1u)) * (1.0 / 4294967296.0);
    float v = 0.7 + 0.3 * float(hash(seed + 2u)) * (1.0 / 4294967296.0);

    return hasvToRgb(vec3(h, s, v));
}

#endif