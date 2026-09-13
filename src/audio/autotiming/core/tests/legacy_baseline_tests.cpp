#include "AutoTiming.h"
#include "util.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr int LegacyFmodPcm16 = 2;
constexpr int LegacyFmodPcm24 = 3;
constexpr int LegacyFmodPcmFloat = 5;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireNear(double actual, double expected, double tolerance, const std::string& message)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(
            message + ": expected " + std::to_string(expected) +
            ", got " + std::to_string(actual));
    }
}

std::vector<std::int16_t> makePulseTrain(
    int sampleRate,
    double durationSeconds,
    double bpm,
    double firstPulseSeconds)
{
    const std::size_t sampleCount =
        static_cast<std::size_t>(std::ceil(durationSeconds * sampleRate));
    std::vector<double> signal(sampleCount, 0.0);

    // A deterministic, low-level bed keeps all legacy sub-band medians finite.
    std::uint32_t noiseState = 0x6d2b79f5U;
    for (std::size_t i = 0; i < sampleCount; ++i) {
        noiseState = noiseState * 1664525U + 1013904223U;
        const double noise = static_cast<double>((noiseState >> 8) & 0xffffU) / 32767.5 - 1.0;
        signal[i] = noise * 0.0005;
    }

    const double secondsPerBeat = 60.0 / bpm;
    for (double beat = firstPulseSeconds; beat < durationSeconds; beat += secondsPerBeat) {
        const std::size_t onset = static_cast<std::size_t>(std::llround(beat * sampleRate));
        const std::size_t burstLength = static_cast<std::size_t>(0.08 * sampleRate);
        for (std::size_t j = 0; j < burstLength && onset + j < sampleCount; ++j) {
            const double t = static_cast<double>(j) / sampleRate;
            const double envelope = std::exp(-t * 55.0);
            const double low = std::sin(2.0 * 3.14159265358979323846 * 90.0 * t);
            const double high = std::sin(2.0 * 3.14159265358979323846 * 1800.0 * t);
            signal[onset + j] += envelope * (0.72 * low + 0.28 * high);
        }
    }

    std::vector<std::int16_t> pcm(sampleCount);
    std::transform(signal.begin(), signal.end(), pcm.begin(), [](double sample) {
        const double clipped = std::max(-1.0, std::min(1.0, sample));
        return static_cast<std::int16_t>(std::llround(clipped * 32767.0));
    });
    return pcm;
}

void testPcmConversion()
{
    const std::int16_t stereo[] = {
        32767, -32768,
        16384, 16384,
    };
    const auto mono = convertToMono(
        reinterpret_cast<const char*>(stereo),
        static_cast<std::uint32_t>(sizeof(stereo)),
        SampleFormat::Signed16LE,
        2);

    require(mono.size() == 2, "16-bit stereo conversion returned the wrong frame count");
    requireNear(mono[0], -1.0 / 65536.0, 1.0e-6, "16-bit channel averaging changed");
    requireNear(mono[1], 0.5, 1.0e-5, "16-bit sample scaling changed");

    const float floatStereo[] = {
        0.25f, 0.75f,
        -0.5f, 0.25f,
    };
    const auto floatMono = convertToMono(
        reinterpret_cast<const char*>(floatStereo),
        static_cast<std::uint32_t>(sizeof(floatStereo)),
        SampleFormat::Float32LE,
        2);

    require(floatMono.size() == 2, "float stereo conversion returned the wrong frame count");
    requireNear(floatMono[0], 0.5, 1.0e-7, "float channel averaging changed");
    requireNear(floatMono[1], -0.125, 1.0e-7, "float channel averaging changed");
}

void testLegacyFormatValidation()
{
    const float samples[] = {0.0f, 0.0f};
    bool threw = false;
    try {
        (void)AutoTiming::detectOnset(
            reinterpret_cast<const char*>(samples),
            static_cast<std::uint32_t>(sizeof(samples)),
            999,
            44100,
            1);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    require(threw, "legacy FMOD adapter accepted an unknown format value");
}

void testFixedTempoBaseline()
{
    constexpr int sampleRate = 44100;
    constexpr double expectedBpm = 120.0;
    const auto pcm = makePulseTrain(sampleRate, 24.0, expectedBpm, 0.125);

    const auto result = AutoTiming::detect(
        reinterpret_cast<const char*>(pcm.data()),
        static_cast<std::uint32_t>(pcm.size() * sizeof(pcm.front())),
        LegacyFmodPcm16,
        sampleRate,
        1);

    require(result.bpm > 0.0, "legacy detector rejected the fixed-tempo fixture");
    requireNear(result.bpm, expectedBpm, 0.5, "legacy fixed-tempo BPM changed");
    require(std::isfinite(result.rawBpm), "legacy raw BPM is not finite");
    require(std::isfinite(result.rawBpmUncertainty), "legacy BPM uncertainty is not finite");
    require(std::isfinite(result.offset), "legacy offset is not finite");
}

void testFloatFormatCompatibilityValue()
{
    // These reach decodeFmod with the historical FMOD mirrored values (3, 5).
    // The unsupported sample rate is intentional: successful decoding must then
    // fail at preprocessing with invalid_argument, not at format dispatch.
    const std::uint8_t pcm24[] = {0, 0, 0, 0, 0, 0};
    bool pcm24ReachedPreprocess = false;
    try {
        (void)AutoTiming::detectOnset(
            reinterpret_cast<const char*>(pcm24),
            static_cast<std::uint32_t>(sizeof(pcm24)),
            LegacyFmodPcm24,
            12345,
            1);
    } catch (const std::invalid_argument&) {
        pcm24ReachedPreprocess = true;
    }
    require(pcm24ReachedPreprocess, "legacy PCM24 FMOD compatibility value changed");

    const float samples[] = {0.0f, 0.0f};
    bool floatReachedPreprocess = false;
    try {
        (void)AutoTiming::detectOnset(
            reinterpret_cast<const char*>(samples),
            static_cast<std::uint32_t>(sizeof(samples)),
            LegacyFmodPcmFloat,
            12345,
            1);
    } catch (const std::invalid_argument&) {
        floatReachedPreprocess = true;
    }
    require(floatReachedPreprocess, "legacy float FMOD compatibility value changed");
}

} // namespace

int main()
{
    try {
        testPcmConversion();
        testLegacyFormatValidation();
        testFloatFormatCompatibilityValue();
        testFixedTempoBaseline();
    } catch (const std::exception& error) {
        std::cerr << "legacy baseline test failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "legacy baseline tests passed\n";
    return 0;
}
