#pragma once

#include <algorithm>
#include <climits>
#include <cstddef>

// Signed integer type for POT array indices, sizes and pointer
// arithmetic. Its size (32-/64-bit) depends on the CPU architecture.
// This should be used for all CSAMLE operations since it is fast and
// allows compiler auto vectorizing. For Qt container operations use
// just int as before.
typedef std::ptrdiff_t SINT;

// 16-bit integer sample data within the asymmetric
// range [SHRT_MIN, SHRT_MAX].
typedef short int SAMPLE;
constexpr SAMPLE SAMPLE_ZERO = 0;
constexpr SAMPLE SAMPLE_MINIMUM = SHRT_MIN;
constexpr SAMPLE SAMPLE_MAXIMUM = SHRT_MAX;


// 32-bit single precision floating-point sample data
// normalized within the range [-1.0, 1.0] with a peak
// amplitude of 1.0. No min/max constants here to
// emphasize the symmetric value range of CSAMPLE
// data!
using CSAMPLE = float;
constexpr CSAMPLE CSAMPLE_ZERO = 0.0f;
constexpr CSAMPLE CSAMPLE_ONE = 1.0f;
constexpr CSAMPLE CSAMPLE_PEAK = CSAMPLE_ONE;
static_assert(sizeof(CSAMPLE) == 4); // 32 bits == 4 bytes

// Limits the range of a CSAMPLE value to [-CSAMPLE_PEAK, CSAMPLE_PEAK].
constexpr CSAMPLE CSAMPLE_clamp(CSAMPLE in) {
    static_assert(-CSAMPLE_PEAK <= CSAMPLE_PEAK);
    return std::clamp(in, -CSAMPLE_PEAK, CSAMPLE_PEAK);
}

// Gain values for weighted calculations of CSAMPLE
// data in the range [0.0, 1.0]. Same data type as
// CSAMPLE to avoid type conversions in calculations.
using CSAMPLE_GAIN = CSAMPLE;
constexpr float CSAMPLE_GAIN_ZERO = CSAMPLE_ZERO;
constexpr float CSAMPLE_GAIN_ONE = CSAMPLE_ONE;
constexpr float CSAMPLE_GAIN_MIN = CSAMPLE_GAIN_ZERO;
constexpr float CSAMPLE_GAIN_MAX = CSAMPLE_GAIN_ONE;

// Limits the range of a CSAMPLE_GAIN value to [CSAMPLE_GAIN_MIN, CSAMPLE_GAIN_MAX].
constexpr CSAMPLE_GAIN CSAMPLE_GAIN_clamp(CSAMPLE_GAIN in) {
    static_assert(CSAMPLE_GAIN_MIN <= CSAMPLE_GAIN_MAX);
    return std::clamp(in, CSAMPLE_GAIN_MIN, CSAMPLE_GAIN_MAX);
}
