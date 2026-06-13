#pragma once

#include "platform.h"
#include <cstdint>

#ifdef ARCH_X86_GENERIC
#include <mmintrin.h>
#include <emmintrin.h>
#endif
#include <vector>
#include <cassert>
#include <math.h>
#include <string>

/// Compute ceil(log2(n)). Returns INT_MIN for n = 0.
inline int ceillog2(uint32_t n)
{
    if (n == 0)
    {
        return INT_MIN;
    }
    else if (n == 1)
    {
        return 0;
    }
    --n;
    int ret = 1;
    for (int i = 4; i > 0; i--)
    {
        uint32_t tmp = n >> (1 << i);
        if (tmp)
        {
            n = tmp;
            ret += 1 << i;
        }
    }
    ret += n >> 1;
    return ret;
}

/// Round double to int32 using SSE cvtsd2si when available.
#if defined(COMPILER_MSVC) && defined(ARCH_X86_GENERIC)
inline int32_t i32rint(const double& x)
{
    return _mm_cvtsd_si32(_mm_load_sd(&x));
}
#else
#define i32rint lrint
#endif

/// Signed remainder (fmod that rounds to nearest).
/// MSVC < 1800 fallback: incorrect for exact half-integer cases, but sufficient here.
#if defined(_MSC_VER) && _MSC_VER < 1800
inline double at_remainder(double x, double y)
{
    double r = fmod(x, y);
    if (fabs(r) * 2.0 > fabs(y))
    {
        r -= _copysign(y, r);
    }
    return r;
}
#else
#define at_remainder remainder
#endif

/// Linear interpolation between (x0,y0) and (x1,y1) at x.
inline double interp(double x0, double y0, double x1, double y1, double x)
{
    return (x - x1) / (x0 - x1) * y0 + (x - x0) / (x1 - x0) * y1;
}

/// Quadratic interpolation to locate the x-coordinate of the peak,
/// given three equally-spaced points.
/// @param x0   x-coordinate of the center point.
/// @param qx   spacing between points.
/// @param ym1  y-value at x0 - qx.
/// @param y0   y-value at x0.
/// @param y1   y-value at x0 + qx.
/// @return estimated x-coordinate of the interpolated extremum.
inline double peak(double x0, double qx, double ym1, double y0, double y1)
{
    return x0 + 0.5 * (y1 - ym1) / ((y0 - y1) + (y0 - ym1)) * qx;
}

/// Quadratic interpolation to estimate the peak value,
/// given three equally-spaced points.
/// @param ym1  y-value at center - 1.
/// @param y0   y-value at center.
/// @param y1   y-value at center + 1.
/// @return estimated peak y-value.
inline double peakv(double ym1, double y0, double y1)
{
    return y0 + 0.125 * (y1 - ym1) * (y1 - ym1) / ((y0 - y1) + (y0 - ym1));
}

/// Approximate median using histogram bins.
/// Negative values are treated as zero. NaNs are not handled.
/// @param x    input array.
/// @param bin  bin resolution exponent; range -22..8; lower = more precise but slower.
/// @return approximate median value.
float medianApprox(const std::vector<float>& x, int bin);
static_assert(sizeof(float) == sizeof(uint32_t), "float is not 32-bit");

/// Non-linear compression: y[i + delay] += ln(x[i] * mul + 1) * weight (approximate).
/// @param x      input array.
/// @param y      output array (accumulated).
/// @param mul    multiplier applied before log.
/// @param weight scaling factor.
/// @param delay  sample offset applied before accumulation.
void compressApprox(const std::vector<float>& x, std::vector<float>& y,
                    float mul, float weight, int delay);

/// Supported PCM sample formats.
enum class SampleFormat
{
    Signed16LE,
    Signed24LE,
    Float32LE,
};

/// Convert multi-channel PCM to averaged mono float samples.
/// Channels are arithmetic-mean averaged; no spatial processing.
/// @param data     raw PCM byte buffer.
/// @param size     byte count of data.
/// @param format   sample format.
/// @param channels number of interleaved channels.
/// @return mono float samples, one per multi-channel frame.
std::vector<float> convertToMono(const char* data, uint32_t size,
                                 SampleFormat format, unsigned channels);