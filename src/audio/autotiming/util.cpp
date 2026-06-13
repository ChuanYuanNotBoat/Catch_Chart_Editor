#include "util.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

float medianApprox(const std::vector<float>& x, int bin)
{
    int roundBits = 23 + bin;
    uint32_t roundOffset = UINT32_C(1) << (roundBits - 1);

    // Build histogram.
    std::vector<size_t> count((1 << (8 - bin)) + 1, 0);
    for (float f : x)
    {
        uint32_t t;
        memcpy(&t, &f, sizeof(float));
        t &= (t >> 31) - 1U;
        ++count[(t + roundOffset) >> roundBits];
    }

    // Find the bin containing the median.
    size_t c = 0;
    for (uint32_t i = 0; ; i++)
    {
        c += count[i];
        if (c >= x.size() / 2)
        {
            uint32_t t = i << roundBits;
            float f;
            memcpy(&f, &t, sizeof(float));
            return f;
        }
    }
}

#ifdef ARCH_X86_GENERIC
void compressApproxSSE(const std::vector<float>& x, std::vector<float>& y,
                       float mul, float weight, int delay)
{
    if (x.size() < abs(delay) + 8U)
    {
        return;
    }
    __m128 mMul = _mm_set_ps1(mul);
    // weight * ln(2) * 2^-23
    __m128 mWeight = _mm_set_ps1(weight * 8.26295829e-8f);

    // Align to 16-byte boundaries.
    const float* xbegin = reinterpret_cast<const float*>(
        reinterpret_cast<uintptr_t>(x.data() - std::min(delay, 0)) + 15 & -16);
    const float* xend = reinterpret_cast<const float*>(
        reinterpret_cast<uintptr_t>(x.data() + x.size() - std::max(delay, 0)) & -16);
    float* ybegin = reinterpret_cast<float*>(
        reinterpret_cast<uintptr_t>(y.data()) + 15 & -16);
    float* yend = reinterpret_cast<float*>(
        reinterpret_cast<uintptr_t>(y.data() + y.size()) & -16);

    size_t len = std::min(xend - xbegin, yend - ybegin);
    for (size_t i = 0; i < len; i += 4)
    {
        // x * mul + 1
        __m128 mx = _mm_load_ps(&xbegin[i]);
        __m128 m = _mm_add_ps(_mm_mul_ps(mx, mMul), _mm_set_ps1(1.0f));
        // Approx. log2(x * mul + 1) * 2^23
        __m128i mi = _mm_castps_si128(m);
        mi = _mm_sub_epi32(mi, _mm_castps_si128(_mm_set_ps1(1.0f)));
        m = _mm_cvtepi32_ps(mi);
        // y += approx. ln(x * mul + 1) * weight
        __m128 my = _mm_load_ps(&ybegin[i]);
        my = _mm_add_ps(_mm_mul_ps(m, mWeight), my);
        _mm_store_ps(&ybegin[i], my);
    }
}
#endif

void compressApprox(const std::vector<float>& x, std::vector<float>& y,
                    float mul, float weight, int delay)
{
    if (x.size() <= abs(delay))
    {
        return;
    }
#if defined(ARCH_X86_GENERIC)
    compressApproxSSE(x, y, mul, weight, delay);
// #elif defined(ARCH_ARM_NEON)
#else
    // Portable fallback: approximate ln(x * mul + 1) via frexp.
    float wln2 = weight * 0.69314718056f;
    const float* xbegin = x.data() - std::min(delay, 0);
    float* ybegin = y.data() + std::max(delay, 0);
    size_t len = x.size() - abs(delay);
    for (size_t i = 0; i < len; i++)
    {
        float mxp1 = xbegin[i] * mul + 1.0f;
        int expo;
        float mag = frexp(mxp1, &expo);
        ybegin[i] += (float(expo - 2) + mag * 2.0f) * wln2;
    }
#endif
}

std::vector<float> convertToMono(const char* data, uint32_t size,
                                 SampleFormat format, unsigned channels)
{
    size_t bits;
    switch (format)
    {
    case SampleFormat::Signed16LE:
        bits = 16;
        break;
    case SampleFormat::Signed24LE:
        bits = 24;
        break;
    case SampleFormat::Float32LE:
        bits = 32;
        break;
    default:
        throw std::invalid_argument("convertToMono: format");
    }

    size_t bytesPerSample = bits * channels / 8;
    if (size % bytesPerSample != 0)
    {
        throw std::runtime_error("convertToMono: Wrong data length");
    }
    size_t samples = size / bytesPerSample;

    std::vector<float> mono(samples, 0.0f);
    switch (format)
    {
    case SampleFormat::Signed16LE:
    {
        const int16_t* x = reinterpret_cast<const int16_t*>(data);
        for (size_t i = 0, j = 0; i < samples; i++)
        {
            for (unsigned c = 0; c < channels; c++, j++)
            {
                mono[i] += x[j] * 3.05175781e-5f;
            }
        }
    }
    break;
    case SampleFormat::Signed24LE:
    {
        const uint8_t* d = reinterpret_cast<const uint8_t*>(data);
        for (size_t i = 0, j = 0; i < samples; i++)
        {
            for (unsigned c = 0; c < channels; c++, j += 3)
            {
                mono[i] += (d[j] | d[j + 1] << 8 |
                    *reinterpret_cast<const int8_t*>(&d[j + 2]) << 16) * 1.19209290e-7f;
            }
        }
    }
    break;
    case SampleFormat::Float32LE:
    {
        const float* x = reinterpret_cast<const float*>(data);
        for (size_t i = 0, j = 0; i < samples; i++)
        {
            for (unsigned c = 0; c < channels; c++, j++)
            {
                mono[i] += x[j];
            }
        }
    }
    break;
    }

    if (channels > 1)
    {
        float mul = 1.0f / channels;
        for (float& x : mono)
        {
            x *= mul;
        }
    }
    return mono;
}