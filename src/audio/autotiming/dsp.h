#pragma once

#include <vector>
#include "fft.h"

/// Apply cascaded second-order IIR sections (direct form II) in-place.
/// @param sections number of biquad sections.
/// @param coeff    array of [b1, b2, -a1, -a2, gain] for each section.
/// @param x        input/output signal.
void filterSos(unsigned sections, const double (*coeff)[5], std::vector<float> &x);

/// Compute auto-correlation via FFT.
/// @param x   input signal.
/// @param len number of output lags.
/// @return auto-correlation for lags 0..len-1.
std::vector<float> autocorr(const std::vector<float> &x, size_t len);

/// Compute cross-correlation using a centered Hann-windowed subset.
/// @param x      first input.
/// @param y      second input.
/// @param maxlen maximum length of the subset.
/// @return cross-correlation.
std::vector<float> xcorrpart(const std::vector<float> &x,
                             const std::vector<float> &y, size_t maxlen);

/// Pearson correlation coefficient at a given delay.
/// @param x     first signal.
/// @param y     second signal.
/// @param delay sample offset (x relative to y).
/// @return correlation coefficient r in [-1, 1].
double corr(const std::vector<float> &x, const std::vector<float> &y, int delay);

/// Nearest-neighbour resampling (no anti-aliasing filter).
/// @param x    input signal.
/// @param rate ratio of output sample rate / input sample rate.
/// @return resampled signal.
std::vector<float> resample(const std::vector<float> &x, double rate);