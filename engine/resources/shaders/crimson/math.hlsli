#ifndef CRIMSON_MATH_HLSLI
#define CRIMSON_MATH_HLSLI

static const float PI = 3.14159265359;
static const float TAU = 6.28318530718;       // A full turn, 2 * PI
static const float HALF_PI = 1.57079632679;
static const float INV_PI = 0.31830988618;
static const float E = 2.71828182846;
static const float INV_SQRT3 = 0.57735026919; // 1 / sqrt(3), each component of the diagonal unit vector (1, 1, 1) / sqrt(3)
static const float EPSILON = 0.000001;        // Small enough to only keep a division from going through zero
static const float FLT_MIN = 1.175494351e-38; // The smallest float, for keeping a value away from 0 without changing any real one
static const float FLT_MAX = 3.402823466e+38; // The largest float, as a starting value when searching for a minimum

#endif
