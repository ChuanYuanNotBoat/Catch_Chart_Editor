#pragma once

#include <complex>

/// Maximum FFT order (2^20 = 1,048,576 points).
const int kFftMaxV = 20;

/// Maximum FFT size.
const size_t kFftMaxN = size_t(1) << kFftMaxV;

/// Complex number type used in FFT computation.
using Complex = std::complex<float>;

/// Initialize cosine/sine lookup tables and bit-reversal index.
/// Must be called once before any FFT computation.
void initFftTables();

/// In-place decimation-in-time FFT.
/// @param v      FFT order (2^v = transform size), must be in [0, kFftMaxV].
/// @param datain  Input array of size 2^v.
/// @param dataout Output array of size 2^v (may alias datain).
void fft(int v, const Complex *datain, Complex *dataout);