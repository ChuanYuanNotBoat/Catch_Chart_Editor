#pragma once

#include <vector>
#include "fft.h"


void filterSos(unsigned sections, const double (* coeff)[5], std::vector<float> & x);

// 只给出部分结果
std::vector<float> autocorr(const std::vector<float> & x, size_t len);

// 只用部分输入进行计算，Hann窗
std::vector<float> xcorrpart(const std::vector<float> & x, const std::vector<float> & y, size_t maxlen);

double corr(const std::vector<float> & x, const std::vector<float> & y, int delay);

// 临近点采样，无滤波
std::vector<float> resample(const std::vector<float> & x, double rate);
