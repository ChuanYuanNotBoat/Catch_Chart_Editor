#include "fft.h"
#include <stdexcept>


namespace {
	const size_t FFT_MAXN_2 = size_t(1) << (FFT_MAXV - 1);
	const double PI = 3.1415926535897932;
	CPLX WTable[FFT_MAXN];
	int BTable[FFT_MAXN];
}


void initFftTables() {
	for(size_t i = 0; i < FFT_MAXN; i++)
	{
		double p = -PI / FFT_MAXN_2 * i;
		WTable[i] = CPLX(cos(p), sin(p));
	}
	BTable[0] = 0;
	for(size_t k = 1, m = FFT_MAXN_2; k < FFT_MAXN; k <<= 1, m >>= 1)
	{
		for(size_t i = 0; i < k; i++)
		{
			BTable[i+k] = BTable[i] + m;
		}
	}
}


void BitReverse(int v, const CPLX * datain, CPLX * dataout)
{
	size_t n = size_t(1) << v;
	size_t m = size_t(1) << (FFT_MAXV - v);
	for(size_t i = 0, j = 0; i < n; i++, j+=m)
	{
		dataout[i] = datain[BTable[j]];
	}
}


void fft(int v, const CPLX * datain, CPLX * dataout)
{
	if (v < 0 || v > FFT_MAXV)
	{
		throw std::invalid_argument("fft");
	}
	size_t n = size_t(1) << v;
	BitReverse(v, datain, dataout);
	for(size_t k = 1, m = FFT_MAXN_2; k < n; k <<= 1, m >>= 1)
	{
		for(size_t l = 0; l < n; l += k << 1)
		{
			CPLX t = dataout[l+k];
			dataout[l+k] = dataout[l] - t;
			dataout[l] += t;
			for(size_t i = 1, j = m; i < k; i++, j+=m)
			{
				CPLX t = dataout[l+i+k] * WTable[j];
				dataout[l+i+k] = dataout[l+i] - t;
				dataout[l+i] += t;
			}
		}
	}
}
