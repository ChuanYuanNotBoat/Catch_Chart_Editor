#pragma once

#include <vector>
#include <queue>
#include <mutex>
#include <atomic>

/// Automatic BPM and offset detection from audio PCM data.
///
/// The pipeline consists of:
///   1. PCM decoding and mono mix-down
///   2. Multi-band spectral energy feature extraction
///   3. Auto-correlation based BPM estimation
///   4. Beat-snap refinement
///   5. Offset calculation via peak alignment
class AutoTiming
{
public:
    struct Result
    {
        double bpm;
        double rawBpm;
        double rawBpmUncertainty;
        unsigned int signature;
        unsigned int division;
        double offset;
    };

    /// Detect BPM and offset from raw PCM buffer.
    /// @param buffer     raw PCM bytes.
    /// @param size       byte count.
    /// @param format     sample format (see FMOD_SOUND_FORMAT_PCMFLOAT etc.).
    /// @param sampleRate audio sample rate (32000, 44100, or 48000).
    /// @param channel    number of interleaved channels.
    /// @return detection result. bpm == 0 indicates failure.
    static Result detect(const char* buffer, uint32_t size, int format,
                         int sampleRate, int channel);

    /// Baseline (lower accuracy) music onset detection for visualization.
    /// @return onset timestamps in seconds.
    static std::vector<double> detectOnset(const char* buffer, uint32_t size,
                                           int format, int sampleRate, int channel);

private:
    static std::vector<float> decodeFmod(const char* buffer, uint32_t size,
                                         int format, int channel);
    static std::vector<float> preprocess(const std::vector<float>& audioData,
                                         unsigned sampleRate);

    /// Compute BPM from the feature signal via auto-correlation peak analysis.
    /// @return 0 = normal, negative = could not compute, positive = low accuracy.
    /// Throws on unrecoverable error.
    static int calcBpm(const std::vector<float>& feature, double& bpm,
                       double& uncertainty, unsigned& signature,
                       unsigned& division);

    static int calcOffset(const std::vector<float>& feature, double bpm,
                          double& offset);

    /// Snap raw BPM to the nearest canonical integer/multiple.
    static double snapBpm(double bpm, double uncertainty);

    static std::once_flag s_flagFftInit;
};