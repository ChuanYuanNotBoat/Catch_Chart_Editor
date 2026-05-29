//
//  AutoTiming.cpp
//  mania
//
//  Created by dolly on 16/1/3.
//
//

#include "AutoTiming.h"
#include <string>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include "util.h"
#include "dsp.h"
#include "fft.h"

using namespace std;

// mirrored from malody LibCpp/src/define.h
enum FMOD_SOUND_FORMAT_LOCAL {
    FMOD_SOUND_FORMAT_NONE_LOCAL = 0,
    FMOD_SOUND_FORMAT_PCM8_LOCAL = 1,
    FMOD_SOUND_FORMAT_PCM16_LOCAL = 2,
    FMOD_SOUND_FORMAT_PCM24_LOCAL = 3,
    FMOD_SOUND_FORMAT_PCM32_LOCAL = 4,
    FMOD_SOUND_FORMAT_PCMFLOAT_LOCAL = 5,
};

#define HE_DEBUG_OUTPUT 0


once_flag AutoTiming::_flagFftInit;


namespace {
	const double FilterDelay = 1.2;
	const double MAX_BPM = 220.0;

	const unsigned SubbandCount = 4;

	const float Weights[] = {
		0.3f,
		0.2f,
		0.2f,
		0.3f,
	};

	const unsigned Filters[][2] = {
		// Subband input filters
		{0, 2},  // <600 Hz 低通
		{2, 4},  // 600~1200 Hz 带通
		{6, 4},  // 1200~2400 Hz 带通
		{10, 2}, // >2400 Hz 高通
		// Subband HF filters
		{12, 1}, // 对最低频段做一个鼓点快速上升缓慢下降的滤波，随手写的参数，以44.1kHz参数为基准
		{0, 0},
		{0, 0},
		{0, 0},
		// LP filter
		{13, 2}, // <300 Hz 低通
		// LF filter
		{15, 1}, // 用于提取上升沿的滤波器的正半部分，随手写的参数，在1kHz下工作
		// BPM detection filter
		{16, 1}, // 大约是4.2~23.8Hz带通，参数的准确来源不记得了，在1kHz下工作
	};

	// 各频带滤波器的延迟补偿，近似估算值，没精确测算
	const int SubbandFilterDelay[] = {
		-320,
		-64,
		-32,
		-0,
	};

	const double FilterCoeffSos44[][5] = {
		// Butterworth LP 4 600/44100
		{2, 1, -1.9296472648815026, 0.93671950987931574, 0.0017680612494532478},
		{2, 1, -1.8470012302151446, 0.85377057373666410, 0.0016923358803798848},
		// Butterworth BP 8 600/44100 1200/44100
		{0, -1, -1.9701832899735581, 0.97774356614503610, 0.042048320411797346},
		{0, -1, -1.9307644878934767, 0.95804211074743828, 0.042048320411797346},
		{0, -1, -1.9237340683861910, 0.93435845766892844, 0.041138010165536872},
		{0, -1, -1.8951712655794619, 0.91375057048949460, 0.041138010165536872},
		// Butterworth BP 8 1200/44100 2400/44100
		{0, -1, -1.9259686517338530, 0.95581997938114360, 0.082730627558081263},
		{0, -1, -1.8121187013381126, 0.91831227931931725, 0.082730627558081263},
		{0, -1, -1.8314525533284556, 0.87252152856112386, 0.079370925545494644},
		{0, -1, -1.7636927184274693, 0.83473849906751341, 0.079370925545494644},
		// Butterworth HP 4 2400/44100
		{-2, 1, -1.6699250371362808, 0.77254617806529502, 0.86061780380039399},
		{-2, 1, -1.4385561035314660, 0.52695903501152652, 0.74137878463574813},
		// B1 HF filtering 44100
		{0, 0, -1.948, 0.9481, 0.048},
		// Butterworth LP 4 300/44100
		{2, 1, -1.9660249635383409, 0.96782223970722425, 0.00044931904222082926},
		{2, 1, -1.9222869522443087, 0.92404424454379519, 0.00043932307487161041},
		// LF filtering 1000
		{0, 0, -1.4, 0.48, 0.2},
		// BPM detection filter 1000
		{0, -1, -1.8799483399273036, 0.88366532316014612, 0.058167338419926939},
	};

	const double FilterCoeffSos48[][5] = {
		// Butterworth LP 4 600/48000
		{2, 1, -1.9357148371211979, 0.94170045160372695, 0.0014964036206322460},
		{2, 1, -1.8590762659582099, 0.86482489876726276, 0.0014371582022632194},
		// Butterworth BP 8 600/48000 1200/48000
		{0, -1, -1.9731491828203316, 0.97953721714347519, 0.038683376541251063},
		{0, -1, -1.9383006635720235, 0.96137281475624847, 0.038683376541251063},
		{0, -1, -1.9305470566393397, 0.93953993813607839, 0.037909869457216396},
		{0, -1, -1.9047367208661594, 0.92047699481829581, 0.037909869457216396},
		// Butterworth BP 8 1200/48000 2400/48000
		{0, -1, -1.9341140031258925, 0.95936683579188464, 0.076211068056843939},
		{0, -1, -1.8345468976158030, 0.92460252591545578, 0.076211068056843939},
		{0, -1, -1.8474800548242554, 0.88234057507713604, 0.073338217988390020},
		{0, -1, -1.7867084943628910, 0.84711889379273642, 0.073338217988390020},
		// Butterworth HP 4 2400/48000
		{-2, 1, -1.7009643319435259, 0.78849973981529797, 0.87236601793970592},
		{-2, 1, -1.4796742169311932, 0.55582154328248878, 0.75887394005342046},
		// B1 HF filtering 48000
		{0, 0, -1.952225, 0.95230941015625, 0.0441},
		// Butterworth LP 4 300/48000
		{2, 1, -1.9688774973857579, 0.97039660175711517, 0.00037977609283935493},
		{2, 1, -1.9285084850826344, 0.92999644239525459, 0.00037198932815510181},
		// LF filtering 1000
		{0, 0, -1.4, 0.48, 0.2},
		// BPM detection filter 1000
		{0, -1, -1.8799483399273036, 0.88366532316014612, 0.058167338419926939},
	};

	const double FilterCoeffSos32[][5] = {
		// Butterworth LP 4 600/32000
		{2, 1, -1.9006465638071275, 0.91391293369153381, 0.0033165924711015399},
		{2, 1, -1.7915876967777840, 0.80409284398316283, 0.0031262868013446823},
		// Butterworth BP 8 600/32000 1200/32000
		{0, -1, -1.9551331294901764, 0.96942359724192628, 0.057589864308760577},
		{0, -1, -1.8914384351956604, 0.94273848531403681, 0.057589864308760577},
		{0, -1, -1.8906481524757157, 0.91056795618768371, 0.055913207754023600},
		{0, -1, -1.8483764376432956, 0.88306736308808476, 0.055913207754023600},
		// Butterworth BP 8 1200/32000 2400/32000
		{0, -1, -1.8832665479670578, 0.93936089151864399, 0.11261473189959464},
		{0, -1, -1.6928128227129573, 0.88994695879396346, 0.11261473189959464},
		{0, -1, -1.7518851739273100, 0.82786888860462982, 0.10660246744674093},
		{0, -1, -1.6489134150085212, 0.77932148942151369, 0.10660246744674093},
		// Butterworth HP 4 2400/32000
		{-2, 1, -1.5182418440638745, 0.70396265666726210, 0.80555112518278416},
		{-2, 1, -1.2554404734849929, 0.40901378318031245, 0.66611356416632628},
		// B1 HF filtering 32000
		{0, 0, -1.9283375, 0.9285274228515625, 0.06615},
		// Butterworth LP 4 300/32000
		{2, 1, -1.9525426196393316, 0.95593497333644439, 0.00084808842427817996},
		{2, 1, -1.8935423413365597, 0.89683218776432150, 0.00082246160694037732},
		// LF filtering 1000
		{0, 0, -1.4, 0.48, 0.2},
		// BPM detection filter 1000
		{0, -1, -1.8799483399273036, 0.88366532316014612, 0.058167338419926939},
	};

	// 如第二行表示当计算的bpm与1/2的倍数相差2.5σ以内时则舍入到1/2的倍数
	const double BpmSnap[][2] = {
		{1.0, 3.0},
		{2.0, 2.5},
		{3.0, 2.0},
		{10.0, 2.0},
		{20.0, 1.5},
		{100.0, 1.5},
		{200.0, 1.0},
	};

}


AutoTiming::AutoTimingResult AutoTiming::detect(const char* buffer, uint32_t size, int format, int sampleRate, int channel) {
	// 初始化FFT的三角函数表
	call_once(_flagFftInit, initFftTables);
	vector<float> audio = decodeFmod(buffer, size, format, channel);
	vector<float> feature = preprocess(audio, sampleRate);
	AutoTimingResult ret;
	int bpmRet = calcBpm(feature, ret.rawBpm, ret.rawBpmUncertainty, ret.signature, ret.division);
	if (bpmRet >= 0) {
		if (bpmRet < 16) {
			ret.bpm = snapBpm(ret.rawBpm, ret.rawBpmUncertainty);
		} else {
			ret.bpm = ret.rawBpm;
		}
	} else {
		ret.bpm = 0;
		ret.offset = 0;
		return ret;
	}

	int offsetRet = calcOffset(feature, ret.bpm, ret.offset);

	if (offsetRet < 0) {
		ret.offset = 0;
		return ret;
	}

	// 转换offset定义，表示把音频延迟多少可以对到从0起算的节拍
	double beatLen = 60000. / ret.bpm;
	ret.offset = beatLen - fmod(ret.offset, beatLen);
	return ret;
}

std::vector<double> AutoTiming::detectOnset(
	const char* buffer, uint32_t size, int format, int sampleRate, int channel) {
	call_once(_flagFftInit, initFftTables);
	const std::vector<float> feature = preprocess(decodeFmod(buffer, size, format, channel), sampleRate);

	// locate peaks
	constexpr std::size_t halfWindowSize = 30;
	constexpr float threshold = 0.5f;

	std::vector<double> result;
	if (feature.size() < halfWindowSize * 2 + 1) {
		return result;
	}

	for (std::size_t i = halfWindowSize; i != feature.size() - halfWindowSize; ++i) {
		const bool isPeak = [&] {
			if (!(feature[i] > threshold)) {
				return false;
			}

			for (std::size_t j = 1; j <= halfWindowSize; ++j) {
				if (!(feature[i - j] < feature[i]) || feature[i] < feature[i + j]) {
					return false;
				}
			}

			return true;
		}();

		if (isPeak) {
			result.push_back(i / 1000.0);
		}
	}

	return result;
}

vector<float> AutoTiming::decodeFmod(const char* buffer, uint32_t size, int format, int channels) {

	switch (format) {
		case FMOD_SOUND_FORMAT_PCM16_LOCAL:
			return convertToMono(buffer, size, SampleFormat::Signed16LE, channels);
		case FMOD_SOUND_FORMAT_PCM24_LOCAL:
			return convertToMono(buffer, size, SampleFormat::Signed24LE, channels);
		case FMOD_SOUND_FORMAT_PCMFLOAT_LOCAL:
			return convertToMono(buffer, size, SampleFormat::Float32LE, channels);
		default:
			throw runtime_error("Unexpected sample format from FMOD");
	}
}

// 提取可能的音符时间点，虽然精度较低，但后续会统计处理
vector<float> AutoTiming::preprocess(const vector<float> & audioData, unsigned sampleRate) {

	const double (* filterCoeffSos)[5];
	switch (sampleRate) {
		case 32000:
			filterCoeffSos = FilterCoeffSos32;
			break;
		case 44100:
			filterCoeffSos = FilterCoeffSos44;
			break;
		case 48000:
			filterCoeffSos = FilterCoeffSos48;
			break;
		default:
			throw invalid_argument("Unsupported sample rate");
	}

	// 分频段取能量
	size_t len = audioData.size();
	vector<float> y(len, 0.0f);
	for (unsigned k = 0; k < SubbandCount; k++) {

		vector<float> x(audioData);
		// 频带滤波
		filterSos(Filters[k][1], &filterCoeffSos[Filters[k][0]], x);
		// 平方（取能量）
		for (size_t i = 0; i < len; i++) {
			x[i] *= x[i];
		}
		// 对最低频段额外滤波处理
		filterSos(Filters[SubbandCount+k][1], &filterCoeffSos[Filters[SubbandCount+k][0]], x);
		// （近似）计算中位数，作为下一步的基准值
		float mul = 2.0f / medianApprox(x, -4);
		// 非线性压缩，然后补偿延迟，把各频段加起来
		compressApprox(x, y, mul, Weights[k], SubbandFilterDelay[k]);
	}

	// 降采样到1kHz
	filterSos(Filters[SubbandCount*2+0][1], &filterCoeffSos[Filters[SubbandCount*2+0][0]], y);
	vector<float> feature = resample(y, 1000.0 / sampleRate);
	len = feature.size();

	// 边缘检测，这个滤波器的延迟会在calcOffset里补偿
	// 做一个时间轴反过来的序列
	y.clear();
	y.resize(len);
	copy(feature.rbegin(), feature.rend(), y.begin());
	// 滤波器的正半部分
	filterSos(Filters[SubbandCount*2+1][1], &filterCoeffSos[Filters[SubbandCount*2+1][0]], feature);
	// 滤波器的负半部分，通过在反序列上滤波来完成
	filterSos(Filters[SubbandCount*2+1][1], &filterCoeffSos[Filters[SubbandCount*2+1][0]], y);
	// 因为整个滤波器是奇函数，所以正负两半相减
	feature[len-1] = -feature[len-2];
	for (int i = len - 2; i > 0; i--)
	{
		feature[i] = y[len-i-2] - feature[i-1];
	}
	feature[0] = y[len-2];

	return feature;
}

int AutoTiming::calcBpm(const vector<float> & feature, double & bpm, double & uncertainty,
						unsigned & signature, unsigned & division) {

	bpm = 0.0;
	size_t len = feature.size();
	// 计算特征的自相关。自相关是τ的函数，代表信号平移τ时长后，和原信号的相关程度
	// 由于音乐节奏的特性，平移整拍、整小节后节拍点会重合，因此前面提取的特征的自相关，会在整拍长度的位置形成一系列峰值
	// 我们通过找出自相关峰值中这样的序列，来计算BPM
	// 最多计算到总长一半的偏移。如果太长就只截取开头一段
	bool tooLong = len + len / 2 > FFT_MAXN;
	vector<float> r;
	if (tooLong) {
		vector<float> part(feature.cbegin(), feature.cbegin() + (FFT_MAXN * 2 / 3));
		r = autocorr(part, part.size() / 2);
	} else {
		r = autocorr(feature, len / 2);
	}

	// 先找出4000ms以内偏移的自相关峰值
	int rlen = min(size_t(4000), r.size());
	int plen = rlen;
	vector<int> rpeak;
	for (int i = 16; i < rlen - 16; i++) {
		if (r[i] > 0.0f) {
			bool peak = true;
			// 要求左右15ms都不超过当前值
			for (int j = 1; j < 16; j++) {
				if (r[i] < r[i-j] || r[i] < r[i+j]) {
					peak = false;
					break;;
				}
			}
			if (peak) {
				rpeak.push_back(i);
#if HE_DEBUG_OUTPUT & 1
				printf("%d %f\n", i, r[i]);
#endif
			}
		}
	}

	// 把这些峰值里的系列找出来，线性回归估计第一个的偏移。还是在4000ms以内偏移里
	size_t estindex;
	float bestravg = 0.0f;
	vector<pair<double, double>> ests;
	for (size_t i = 0; i < rpeak.size() && rpeak[i] < plen; i++)
	{
		float bestr = 0.0f;
		// 低于目前最高峰0.8倍的丢弃
		if (ests.empty() || r[rpeak[i]] > bestr * 0.8f) {
			if (r[rpeak[i]] > bestr) {
				bestr = r[rpeak[i]];
			}
			double m = rpeak[i];
			size_t j = i;
			int p = i32rint(m);
			unsigned n = 0;
			double sxx = 0.0;
			double sxy = 0.0;
			double syy = 0.0;
			unsigned miss = 0;
			float avgpeak = 0.0f;
			// 查找整倍数上的峰值，允许10ms误差，计算这些峰值的均值
			for (double k = 1; p < rlen - 10 && miss <= 0; k++) {
				double pest = p;
				for (; j < rpeak.size() && rpeak[j] < p - 10; j++)
					;
				if (j < rpeak.size() && rpeak[j] <= p + 10) {
					n++;
					p = rpeak[j];
					avgpeak += r[p];
					pest = peak(p, 1, r[p-1], r[p], r[p+1]);
					sxx += k * k;
					sxy += k * pest;
					syy += pest * pest;
					m = sxy / sxx;
				} else {
					miss++;
				}
				p = i32rint(pest + m);
			}

			// 如果有倍数，而且至少一半长度（2000ms）内的倍数都是峰值，则进行处理
			if (n > 0 && p > rlen / 2) {
				avgpeak /= n;
				// 均值超过第一个的0.7倍才算，防止孤立的峰值
				if (avgpeak > r[rpeak[i]] * 0.70f) {
#if HE_DEBUG_OUTPUT & 1
					printf("%c %d %.3f %f\n", avgpeak > bestravg * 1.25f ? '*' : avgpeak > bestravg * 1.00f ? '+' : ' ' ,rpeak[i], sxy / sxx, avgpeak);
#endif
					// 记录均值的上升序列，后面算一小节拍数和一拍细分数会用到
					if (avgpeak > bestravg * 1.00f) {
						ests.push_back(pair<double, double>(sxy / sxx, avgpeak));
						// 均值超过当前最高值1.25倍的，认为可能是真拍长
						// 为什么这么做呢，因为经常是相隔0.5拍、1拍、2拍、4拍的相似性会逐渐增大，但增速会减慢
						// 所以如果保持比前面的相似度有较大的增长，我们认为可能前面是一拍的细分，这里是一整拍
						// 而相似度缓慢的增长，则认为可能是从一拍到了数拍或一小节以上
						if (avgpeak > bestravg * 1.25f) {
							estindex = ests.size() - 1;
						}
					}
					if (avgpeak > bestravg) {
						bestravg = avgpeak;
					}
				}
			}
		}
	}

	if (!(bestravg > 0.0f)) {
		return -1;
	}
	// 限制最高BPM
	double m = ests[estindex].first;
	while (m < 60000.0 / MAX_BPM && estindex < ests.size() - 1 && ests[estindex + 1].first < 60000.0 / MAX_BPM * 2.0) {
		bool success = false;
		for (size_t i = estindex + 1; i < ests.size() && ests[i].first < 60000.0 / MAX_BPM * 2.0; i++) {
			if (fabs(at_remainder(ests[i].first, m)) <= 10.0) {
				estindex = i;
				m = ests[estindex].first;
				success = true;
				break;
			}
		}
		if (!success) {
			break;
		}
	}

	// 试图找出每小节拍数
	signature = 1;
	for (size_t i = estindex + 1; i < ests.size(); i++) {
		if (fabs(at_remainder(ests[i].first, ests[estindex].first)) <= 10.0) {
			signature = i32rint(ests[i].first / ests[estindex].first);
			if (signature > 2) {
				break;
			}
		}
	}

	// 试图找出每拍细分数
	division = 1;
	for (size_t i = estindex - 1; i != size_t(-1); i--) {
		if (fabs(at_remainder(ests[estindex].first, ests[i].first)) <= 10.0) {
			division = i32rint(ests[estindex].first / ests[i].first);
			if (division > 2) {
				break;
			}
		}
	}
#if HE_DEBUG_OUTPUT & 1
	printf("%d 1/%d\n", signature, division);
#endif

	// 最后我们在整（半）个自相关结果上把上面求出的拍长序列都找出来，做线性回归
	size_t p = i32rint(m);
	unsigned n = 0;
	double sx = 0.0;
	double sxx = 0.0;
	double sxy = 0.0;
	double syy = 0.0;
	unsigned miss = 0;
	unsigned contmiss = 0;
	for (double k = 1.0; p + 10 < r.size() && miss <= 0 && contmiss <= 0; k++) {
		size_t maxp = max_element(r.begin() + (p - 10), r.begin() + (p + 11)) - r.begin();
		double pest = p;
		if (r[maxp] > 0.0f && (maxp < p ? p - maxp : maxp - p) <= 8) {
			contmiss = 0;
			n++;
			pest = peak(maxp, 1, r[maxp-1], r[maxp], r[maxp+1]);
			sx += k;
			sxx += k * k;
			sxy += k * pest;
			syy += pest * pest;
			m = sxy / sxx;
#if HE_DEBUG_OUTPUT & 2
			double bpmt = 60.0 * 1000.0 * sxx / sxy;
			double sigmat = sqrt((syy - sxy * sxy / sxx) / (n - 1));
			double sigmabpmt = sqrt((sxx * syy / sxy / sxy - 1.0) / (n - 1)) * bpmt;
			double sigmabpmlt = sqrt((sxx * syy / sxy / sxy - 1.0) / (n - 1) * n) * bpmt;
			//double sigmabpmlt = sigmat / sx * n / sxy * sxx * bpmt;
			printf("%3.0f %9.2f %5.2f %5.2f %.5f %.5f %.2f %.5f %.5f\n", k, pest, pest - k * m, (pest - k * m) / sigmat, m, bpmt, sigmat, sigmabpmt, sigmabpmlt);
#endif
		} else {
			miss++;
			contmiss++;
#if HE_DEBUG_OUTPUT & 2
			printf("%3.0f MISS\n", k);
#endif
		}
		p = i32rint(pest + m);
	}
	bpm = 60000.0 * sxx / sxy;
	double sigma = sqrt((syy - sxy * sxy / sxx) / (n - 1));
	double sigmabpm = sqrt((sxx * syy / sxy / sxy - 1.0) / (n - 1)) * bpm;
	// 为什么要乘以根号N呢，因为和一般线性回归的假设不同，这里自相关峰值点序列的误差之间不是独立的，更接近f^-2噪声
	uncertainty = sqrt((sxx * syy / sxy / sxy - 1.0) / (n - 1) * n) * bpm;
#if HE_DEBUG_OUTPUT & 0x10
	printf("%.5f %.5f %d %d %.2f %.5f %.2f %.5f %.5f\n", m, sxy / sxx, n, miss, double(p) / len, bpm, sigma, sigmabpm, uncertainty);
#endif

	// 简单判断一下计算结果的可信度
	if (n < 4) {
		return -1;
	}
	if (sigma > 2.4 || uncertainty / bpm > 0.00005) {
		return 16;
	}
	if (miss > 0 || sigma > 0.6) {
		return 1;
	}
	return 0;
}


// 试图对齐到整BPM
double AutoTiming::snapBpm(double bpm, double uncertainty) {
	for (size_t i = 0; i < sizeof(BpmSnap) / sizeof(BpmSnap[0]); i++) {
#if HE_DEBUG_OUTPUT & 0x20
		printf("%.3f %.2f\n", BpmSnap[i][0], at_remainder(bpm, 1.0 / BpmSnap[i][0]) / sigmabpml);
#endif
		if (fabs(at_remainder(bpm, 1.0 / BpmSnap[i][0])) < uncertainty * BpmSnap[i][1]) {
#if HE_DEBUG_OUTPUT & 0x20
			printf("%.5f %.5f\n", bpm, i32rint(bpm * BpmSnap[i][0]) / BpmSnap[i][0]);
#endif
			bpm = i32rint(bpm * BpmSnap[i][0]) / BpmSnap[i][0];
			break;
		}
	}
	return bpm;
}


int AutoTiming::calcOffset(const vector<float> & feature, double bpm, double & offset) {

	if (!(bpm > 0.0)) {
		return -1;
	}
	size_t len = feature.size();
	// 把特征以60000/BPM为间隔全部累加起来
	double spb = 60000.0 / bpm;
	size_t slen = static_cast<size_t>(ceil(spb)) + 10;
	vector<float> x(slen, 0.0f);
	for (size_t i = 0; i < ceil(len / spb); i++) {
		size_t k = i32rint(spb * i);
		for (size_t j = 0; j < slen && j < len - k; j++) {
			x[j] += feature[k + j];
		}
	}
	// 直接找峰值，简单粗暴
	int maxp = max_element(x.begin() + 5, x.end() - 5) - x.begin();
	offset = peak(maxp, 1.0, x[maxp-1], x[maxp], x[maxp+1]);
#if HE_DEBUG_OUTPUT & 4
	double rp = peakv(x[maxp-1], x[maxp], x[maxp+1]);
	printf("%d %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", maxp, rp, offset, x[maxp-2], x[maxp-1], x[maxp], x[maxp+1], x[maxp+2]);
#endif
	// 补偿边缘检测滤波器的延迟
	offset -= FilterDelay;
	return 0;
}
