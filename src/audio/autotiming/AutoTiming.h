//
//  AutoTiming.hpp
//  mania
//
//  Created by dolly on 16/1/3.
//
//

#ifndef AutoTiming_hpp
#define AutoTiming_hpp

#include <vector>
#include <queue>
#include <mutex>
#include <atomic>

class AutoTiming{
public:
	enum AutoTimingEvent {
		AutoTiming_Result,		// const char *，结果在resultQueue里
		AutoTiming_Error,		// const std::pair<const char *, uint32_t> *，预留错误提示的接口，未实装
		AutoTiming_Progress,	// const std::pair<const char *, double> *
	};

	struct AutoTimingResult{
		double bpm;
		double rawBpm;
		double rawBpmUncertainty;
		unsigned int signature;
		unsigned int division;
		double offset;
	};


private:
	
	static std::vector<float> decodeFmod(const char* buffer, uint32_t size, int format, int channel);
	static std::vector<float> preprocess(const std::vector<float> & audioData, unsigned sampleRate);
	/*
	 * 返回值：
	 * 0		正常
	 * 负		算不出
	 * 正		精度低
	 * 抛异常	出错
	 */
	static int calcBpm(const std::vector<float> & feature, double & bpm, double & uncertainty,
		unsigned & signature, unsigned & division);
	static int calcOffset(const std::vector<float> & feature, double bpm, double & offset);
	
	static double snapBpm(double bpm, double uncertainty);
public:
	static AutoTimingResult detect(const char* buffer, uint32_t size, int format, int sampleRate, int channel);

	// baseline (low accuracy) music onset detection
	static std::vector<double> detectOnset(const char* buffer, uint32_t size, int format, int sampleRate, int channel);

private:
	static std::once_flag _flagFftInit;
};


#endif /* AutoTiming_hpp */
