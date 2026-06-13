#include "fft.h"
#include <stdexcept>

namespace
{

const size_t kFftMaxN2 = size_t(1) << (kFftMaxV - 1);
const double kPi = 3.1415926535897932;

Complex wTable[kFftMaxN];
int bTable[kFftMaxN];

} // namespace

void initFftTables()
{
    for (size_t i = 0; i < kFftMaxN; i++)
    {
        double p = -kPi / kFftMaxN2 * i;
        wTable[i] = Complex(cos(p), sin(p));
    }
    bTable[0] = 0;
    for (size_t k = 1, m = kFftMaxN2; k < kFftMaxN; k <<= 1, m >>= 1)
    {
        for (size_t i = 0; i < k; i++)
        {
            bTable[i + k] = bTable[i] + m;
        }
    }
}

void BitReverse(int v, const Complex* datain, Complex* dataout)
{
    size_t n = size_t(1) << v;
    size_t m = size_t(1) << (kFftMaxV - v);
    for (size_t i = 0, j = 0; i < n; i++, j += m)
    {
        dataout[i] = datain[bTable[j]];
    }
}

void fft(int v, const Complex* datain, Complex* dataout)
{
    if (v < 0 || v > kFftMaxV)
    {
        throw std::invalid_argument("fft");
    }
    size_t n = size_t(1) << v;
    BitReverse(v, datain, dataout);
    for (size_t k = 1, m = kFftMaxN2; k < n; k <<= 1, m >>= 1)
    {
        for (size_t l = 0; l < n; l += k << 1)
        {
            Complex t = dataout[l + k];
            dataout[l + k] = dataout[l] - t;
            dataout[l] += t;
            for (size_t i = 1, j = m; i < k; i++, j += m)
            {
                Complex t = dataout[l + i + k] * wTable[j];
                dataout[l + i + k] = dataout[l + i] - t;
                dataout[l + i] += t;
            }
        }
    }
}