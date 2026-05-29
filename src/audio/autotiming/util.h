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


inline int ceillog2(uint32_t n) {
    if (n == 0) {
        return INT_MIN;
    } else if (n == 1) {
        return 0;
    }
    --n;
    int ret = 1;
    for (int i = 4; i > 0; i--) {
        uint32_t tmp = n >> (1 << i);
        if (tmp) {
            n = tmp;
            ret += 1 << i;
        }
    }
    ret += n >> 1;
    return ret;
}


#if defined(COMPILER_MSVC) && defined(ARCH_X86_GENERIC)
inline int32_t i32rint(const double & x) {
    return _mm_cvtsd_si32(_mm_load_sd(&x));
}
#else
#define i32rint lrint
#endif


#if defined(_MSC_VER) && _MSC_VER < 1800
// 余数正好是一半时的处理不正确，不过用在这里够了
inline double at_remainder(double x, double y) {
    double r = fmod(x, y);
    if (fabs(r) * 2.0 > fabs(y)) {
        r -= _copysign(y, r);
    }
    return r;
}
#else
#define at_remainder remainder
#endif


// 线性插值
inline double interp(double x0, double y0, double x1, double y1, double x) {
    return (x - x1) / (x0 - x1) * y0 + (x - x0) / (x1 - x0) * y1;
}


// 二次函数插值找极值点对应的x
inline double peak(double x0, double qx, double ym1, double y0, double y1) {
    return x0 + 0.5 * (y1 - ym1) / ((y0 - y1) + (y0 - ym1)) * qx;
}


// 二次函数插值找极值
inline double peakv(double ym1, double y0, double y1) {
    return y0 + 0.125 * (y1 - ym1) * (y1 - ym1) / ((y0 - y1) + (y0 - ym1));
}


// 负数一概作为0处理，不考虑NaN
// bin范围-22~8，表示相对精度约为2^bin，越低越精确，但需要更多时间/内存
float medianApprox(const std::vector<float> & x, int bin);
static_assert(sizeof(float) == sizeof(uint32_t), "float isn't 32-bit");


// 各种近似
void compressApprox(const std::vector<float> & x, std::vector<float> & y, float mul, float weight, int delay);


enum class SampleFormat {
    Signed16LE,
    Signed24LE,
    Float32LE,
};

// 我们就不管多声道配置了，就简单算术平均，反正这里用到的应该都是2声道
std::vector<float> convertToMono(const char* data, uint32_t size, SampleFormat format, unsigned channels);
