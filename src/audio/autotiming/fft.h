#pragma once

#include <complex>

const int FFT_MAXV = 20;
const size_t FFT_MAXN = size_t(1) << FFT_MAXV;

//typedef std::complex<double> CPLX;
typedef std::complex<float> CPLX;


void initFftTables();
void fft(int v, const CPLX * datain, CPLX * dataout);
