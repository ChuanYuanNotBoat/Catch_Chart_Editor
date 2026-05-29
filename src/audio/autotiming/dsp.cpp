#include "dsp.h"
#include "util.h"
#include <cmath>
#include <fstream>
#include <algorithm>

using namespace std;
#pragma warning(disable:4996)

namespace {
    const double PI = 3.1415926535897932;
}


// 数字滤波
// Cascaded second-order sections, direct form II
void filterSos(unsigned sections, const double (* coeff)[5], std::vector<float> & x) {
    // Force flushing denormal values to zero
    // Safe for IIR filters
#ifdef ARCH_X86_GENERIC
#ifdef COMPILER_MSVC
    unsigned int oldcw = _controlfp(0, 0);
    _controlfp(oldcw | _DN_FLUSH, _MCW_DN);
#else
    int mxcsr = _mm_getcsr();
    _mm_setcsr(mxcsr | 0x00008000);
#endif
#endif
    size_t len = x.size();
    for (unsigned k = 0; k < sections; k++) {
        float b1 = static_cast<float>(coeff[k][0]);
        float b2 = static_cast<float>(coeff[k][1]);
        float ma1 = static_cast<float>(-coeff[k][2]);
        float ma2 = static_cast<float>(-coeff[k][3]);
        float g = static_cast<float>(coeff[k][4]);
        float s1 = 0.0f;
        float s2 = 0.0f;
        for (size_t i = 0; i < len; i++) {
            float s0 = x[i] * g + s1 * ma1 + s2 * ma2;
            x[i] = s0 + s1 * b1 + s2 * b2;
            s2 = s1;
            s1 = s0;
        }
    }
    // Reset FP flags
#ifdef ARCH_X86_GENERIC
#ifdef COMPILER_MSVC
    _controlfp(oldcw, _MCW_DN);
#else
    _mm_setcsr(mxcsr);
#endif
#endif
}


vector<float> autocorr(const vector<float> & x, size_t len) {
    int v = ceillog2(x.size() + len - 1);
    size_t n = size_t(1) << v;
    vector<CPLX> a(n);
    vector<CPLX> b(n);
    for (size_t i = 0; i < x.size(); i++) {
        a[i] = CPLX(x[i]);
    }
    fft(v, a.data(), b.data());
    for (size_t i = 0; i < n; i++) {
        a[i] = CPLX(b[i].real() * b[i].real() + b[i].imag() * b[i].imag());
    }
    fft(v, a.data(), b.data());
    vector<float> y(len);
    float ifftg = 1.0f / n;
    for (size_t i = 0; i < len; i++) {
        y[i] = b[i].real() * ifftg;
    }
    return y;
}


vector<float> xcorrpart(const vector<float> & x, const vector<float> & y, size_t maxlen) {
    size_t len = std::min(std::min(x.size(), y.size()), maxlen);
    size_t pos = (std::min(x.size(), y.size()) - len) / 2;
    int v = ceillog2(len * 2);
    size_t n = size_t(1) << v;
    vector<CPLX> a(n);
    vector<CPLX> b(n);
    vector<CPLX> c(n);
    for (size_t i = 0; i < len; i++)
    {
        float w = static_cast<float>(1.0 - cos((i + 0.5) / len * 2 * PI));
        a[i] = CPLX(x[pos + i] * w);
        b[i] = CPLX(y[pos + i] * w);
    }
    fft(v, a.data(), c.data());
    fft(v, b.data(), a.data());
    for (size_t i = 0; i < n; i++)
    {
        b[i] = CPLX(c[i].real(), -c[i].imag()) * a[i];
    }
    fft(v, b.data(), c.data());
    vector<float> ret(n);
    float ifftg = 1.0f / n;
    for (size_t i = 0; i < n; i++)
    {
        ret[i] = c[i].real() * ifftg;
    }
    return ret;
}


double corr(const vector<float> & x, const vector<float> & y, int delay) {
    double sxx = 0.0;
    double sxy = 0.0;
    double syy = 0.0;
    for (size_t i = std::max(-delay, 0); i < std::min(x.size() - delay, y.size()); i++) {
        double xd = x[i + delay];
        double yd = y[i];
        sxx += xd * xd;
        sxy += xd * yd;
        syy += yd * yd;
    }
    return sxy / sqrt(sxx * syy);
}


vector<float> resample(const vector<float> & x, double rate) {
    vector<float> y(static_cast<size_t>(ceil(x.size() * rate)));
    for (size_t i = 0; i < y.size(); i++) {
        size_t k = i32rint(i / rate);
        if (k >= x.size())
        {
            y[i] = 0;
        }
        else
        {
            y[i] = x[k];
        }
    }
    return y;
}
