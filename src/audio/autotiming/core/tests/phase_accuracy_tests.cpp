// AutoTiming 2 extension developed from the Malody legacy baseline.
// Provenance and licensing-scope notes: ATTRIBUTION.md.

#include "autotiming/Analysis.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr double Pi = 3.14159265358979323846;

enum class TransientShape {
    Click,
    LowBurst,
    HighBurst,
    NoiseBurst,
};

struct Fixture {
    std::string name;
    std::uint32_t sampleRate = 44100;
    double durationSeconds = 20.0;
    double bpm = 120.0;
    double firstPulseSeconds = 0.125;
    TransientShape shape = TransientShape::Click;
    std::uint32_t channels = 1;
    double rightChannelDelayMilliseconds = 0.0;
    bool alternatingAccents = false;
    std::uint32_t missingEvery = 0;
    bool addOffbeatGhosts = false;
};

struct Measurement {
    std::string fixture;
    const char* source = "";
    double errorMilliseconds = 0.0;
    double estimatedBpm = 0.0;
    double windowCenterSeconds = 0.0;
};

enum class TempoProfile {
    MicroVariation,
    Rubato,
    Step,
    LinearRamp,
    PhaseReset,
};

struct VariableFixture {
    const char* name = "";
    std::uint32_t sampleRate = 44100;
    double durationSeconds = 40.0;
    TempoProfile profile = TempoProfile::MicroVariation;
    TransientShape shape = TransientShape::Click;
};

void require(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

double sampleValue(
    TransientShape shape,
    double secondsAfterOnset,
    std::uint32_t& noiseState)
{
    const double envelope = std::exp(-secondsAfterOnset * 70.0);
    switch (shape) {
    case TransientShape::Click:
        return envelope;
    case TransientShape::LowBurst:
        return envelope * std::sin(2.0 * Pi * 95.0 * secondsAfterOnset);
    case TransientShape::HighBurst:
        return envelope * std::sin(2.0 * Pi * 3200.0 * secondsAfterOnset);
    case TransientShape::NoiseBurst:
        noiseState = noiseState * 1664525U + 1013904223U;
        return envelope *
            (static_cast<double>((noiseState >> 8) & 0xffffU) / 32767.5 - 1.0);
    }
    return 0.0;
}

std::vector<float> makeFixture(const Fixture& fixture)
{
    const std::size_t frameCount = static_cast<std::size_t>(
        std::ceil(fixture.durationSeconds * fixture.sampleRate));
    std::vector<float> mono(frameCount, 0.0f);

    std::uint32_t backgroundState = 0x75bcd15U;
    for (float& sample : mono) {
        backgroundState = backgroundState * 1664525U + 1013904223U;
        const double noise =
            static_cast<double>((backgroundState >> 8) & 0xffffU) / 32767.5 - 1.0;
        sample = static_cast<float>(noise * 0.0002);
    }

    const double periodSeconds = 60.0 / fixture.bpm;
    const auto addBurst = [&](
        double pulse,
        double gain,
        std::uint32_t seed,
        TransientShape shape) {
        const std::size_t firstSample = static_cast<std::size_t>(
            std::ceil(pulse * fixture.sampleRate));
        const std::size_t burstLength = static_cast<std::size_t>(
            std::ceil(0.060 * fixture.sampleRate));
        std::uint32_t burstState = seed;
        for (std::size_t i = firstSample;
             i < mono.size() && i < firstSample + burstLength;
             ++i) {
            const double time = static_cast<double>(i) / fixture.sampleRate;
            const double value = sampleValue(
                shape,
                time - pulse,
                burstState);
            mono[i] += static_cast<float>(gain * value);
        }
    };

    std::size_t pulseIndex = 0;
    for (double pulse = fixture.firstPulseSeconds;
         pulse < fixture.durationSeconds;
         pulse += periodSeconds, ++pulseIndex) {
        const double accent = fixture.alternatingAccents && pulseIndex % 2 != 0
            ? 0.38
            : 0.85;
        if (fixture.missingEvery == 0 ||
            pulseIndex % fixture.missingEvery != fixture.missingEvery - 1) {
            addBurst(
                pulse,
                accent,
                static_cast<std::uint32_t>(
                    0x9e3779b9U + pulseIndex * 0x85ebca6bU),
                fixture.shape);
        }
        if (fixture.addOffbeatGhosts) {
            addBurst(
                pulse + periodSeconds * 0.5,
                0.14,
                static_cast<std::uint32_t>(
                    0xc2b2ae35U + pulseIndex * 0x27d4eb2fU),
                TransientShape::HighBurst);
        }
    }

    if (fixture.channels == 1) {
        return mono;
    }
    require(fixture.channels == 2, "phase fixture supports only mono or stereo");
    const std::size_t delayFrames = static_cast<std::size_t>(std::llround(
        fixture.rightChannelDelayMilliseconds * fixture.sampleRate / 1000.0));
    std::vector<float> interleaved(frameCount * 2, 0.0f);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        interleaved[frame * 2] = mono[frame];
        if (frame >= delayFrames) {
            interleaved[frame * 2 + 1] = mono[frame - delayFrames] * 0.72f;
        }
    }
    return interleaved;
}

double profileBpm(const VariableFixture& fixture, double timeSeconds)
{
    switch (fixture.profile) {
    case TempoProfile::MicroVariation:
        return 120.0 + 0.8 * std::sin(2.0 * Pi * timeSeconds / 12.0);
    case TempoProfile::Rubato:
        return 112.0 + 5.0 * std::sin(2.0 * Pi * timeSeconds / 16.0);
    case TempoProfile::Step:
        return timeSeconds < fixture.durationSeconds * 0.5 ? 100.0 : 150.0;
    case TempoProfile::LinearRamp:
        return 90.0 + 60.0 * timeSeconds / fixture.durationSeconds;
    case TempoProfile::PhaseReset:
        return 120.0;
    }
    return 120.0;
}

std::vector<double> makeVariablePulseTimes(const VariableFixture& fixture)
{
    std::vector<double> pulses;
    if (fixture.profile == TempoProfile::PhaseReset) {
        const double change = fixture.durationSeconds * 0.5;
        for (double pulse = 0.1373; pulse < change; pulse += 0.5) {
            pulses.push_back(pulse);
        }
        for (double pulse = change + 0.1973;
             pulse < fixture.durationSeconds;
             pulse += 0.5) {
            pulses.push_back(pulse);
        }
        return pulses;
    }
    for (double pulse = 0.1373; pulse < fixture.durationSeconds;) {
        pulses.push_back(pulse);
        pulse += 60.0 / profileBpm(fixture, pulse);
    }
    return pulses;
}

std::vector<float> renderVariableFixture(
    const VariableFixture& fixture,
    const std::vector<double>& pulses)
{
    const std::size_t sampleCount = static_cast<std::size_t>(
        std::ceil(fixture.durationSeconds * fixture.sampleRate));
    std::vector<float> signal(sampleCount, 0.0f);
    std::uint32_t backgroundState = 0x243f6a88U;
    for (float& sample : signal) {
        backgroundState = backgroundState * 1664525U + 1013904223U;
        const double noise =
            static_cast<double>((backgroundState >> 8) & 0xffffU) / 32767.5 - 1.0;
        sample = static_cast<float>(noise * 0.0002);
    }

    const std::size_t burstLength = static_cast<std::size_t>(
        std::ceil(0.060 * fixture.sampleRate));
    for (std::size_t pulseIndex = 0; pulseIndex < pulses.size(); ++pulseIndex) {
        const double pulse = pulses[pulseIndex];
        const std::size_t firstSample = static_cast<std::size_t>(
            std::ceil(pulse * fixture.sampleRate));
        std::uint32_t burstState = static_cast<std::uint32_t>(
            0xb7e15162U + pulseIndex * 0x9e3779b9U);
        for (std::size_t i = firstSample;
             i < signal.size() && i < firstSample + burstLength;
             ++i) {
            const double time = static_cast<double>(i) / fixture.sampleRate;
            signal[i] += static_cast<float>(0.85 * sampleValue(
                fixture.shape,
                time - pulse,
                burstState));
        }
    }
    return signal;
}

double phaseErrorMilliseconds(
    double pulseTimeSeconds,
    double firstPulseSeconds,
    double periodSeconds)
{
    double residual = std::remainder(
        pulseTimeSeconds - firstPulseSeconds,
        periodSeconds);
    residual = std::abs(residual);
    return residual * 1000.0;
}

double percentile(std::vector<double> values, double fraction)
{
    require(!values.empty(), "cannot calculate a percentile of no measurements");
    std::sort(values.begin(), values.end());
    const double position = fraction * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    const double mix = position - static_cast<double>(lower);
    return values[lower] * (1.0 - mix) + values[upper] * mix;
}

std::vector<Measurement> measureFixture(const Fixture& fixture)
{
    const std::vector<float> audio = makeFixture(fixture);
    const autotiming::AudioView view{
        audio.data(),
        audio.size() / fixture.channels,
        fixture.sampleRate,
        fixture.channels,
    };
    autotiming::AnalysisOptions options;
    options.windowSpecs = {
        {autotiming::WindowScale::Short, 8.0, 4.0},
    };
    options.minimumTempoBpm = std::max(30.0, fixture.bpm * 0.72);
    options.maximumTempoBpm = std::min(480.0, fixture.bpm * 1.38);

    const autotiming::AnalysisResult result = autotiming::analyze(view, options);
    std::vector<Measurement> measurements;
    const double periodSeconds = 60.0 / fixture.bpm;
    for (const autotiming::AnalysisWindow& window : result.diagnostics.windows) {
        const autotiming::TempoCandidate* best = nullptr;
        double bestTempoError = std::numeric_limits<double>::infinity();
        for (const autotiming::TempoCandidate& candidate : window.tempoCandidates) {
            if (candidate.origin != autotiming::CandidateOrigin::OnsetEnvelopePeriodicity) {
                continue;
            }
            const double tempoError = std::abs(candidate.bpm / fixture.bpm - 1.0);
            if (tempoError < bestTempoError) {
                best = &candidate;
                bestTempoError = tempoError;
            }
        }
        if (best == nullptr || bestTempoError > 0.02) {
            continue;
        }
        measurements.push_back({
            fixture.name,
            "candidate",
            phaseErrorMilliseconds(
                best->pulseTimeSeconds,
                fixture.firstPulseSeconds,
                periodSeconds),
            best->bpm,
            (window.startSeconds + window.endSeconds) * 0.5,
        });
    }
    std::size_t trackerMeasurements = 0;
    for (const autotiming::TempoTrackPoint& point : result.tempoTrack.points) {
        const double tempoError = point.bpm > 0.0
            ? std::abs(point.bpm / fixture.bpm - 1.0)
            : std::numeric_limits<double>::infinity();
        if (point.state != autotiming::TempoTrackState::Observed || tempoError > 0.02) {
            continue;
        }
        measurements.push_back({
            fixture.name,
            "tracker",
            phaseErrorMilliseconds(
                point.pulseTimeSeconds,
                fixture.firstPulseSeconds,
                periodSeconds),
            point.bpm,
            point.timeSeconds,
        });
        ++trackerMeasurements;
    }
    require(measurements.size() >= 2,
        std::string("too few phase measurements for fixture ") + fixture.name);
    require(trackerMeasurements >= 2,
        std::string("too few tracker phase measurements for fixture ") + fixture.name);
    return measurements;
}

void testSyntheticPhaseMatrix()
{
    std::vector<Fixture> fixtures = {
        {"click-47-44k", 44100, 28.0, 47.0, 0.4173, TransientShape::Click,
            1, 0.0, false, 0, false},
        {"click-60-32k", 32000, 24.0, 60.0, 0.1373, TransientShape::Click,
            1, 0.0, false, 0, false},
        {"low-73-44k", 44100, 24.0, 73.0, 0.2837, TransientShape::LowBurst,
            1, 0.0, false, 0, false},
        {"missing-89-48k", 48000, 24.0, 89.0, 0.6381, TransientShape::NoiseBurst,
            1, 0.0, false, 5, false},
        {"stereo-95-32k", 32000, 24.0, 95.0, 0.9973, TransientShape::Click,
            2, 0.32, true, 0, false},
        {"high-120-48k", 48000, 20.0, 120.0, 0.1267, TransientShape::HighBurst,
            1, 0.0, false, 0, false},
        {"odd-123_456-44k", 44100, 22.0, 123.456, 0.3337, TransientShape::Click,
            1, 0.0, true, 0, false},
        {"ghosts-137-48k", 48000, 22.0, 137.0, 0.0037, TransientShape::NoiseBurst,
            1, 0.0, false, 0, true},
        {"noise-179_5-44k", 44100, 20.0, 179.5, 0.2194, TransientShape::NoiseBurst,
            1, 0.0, false, 0, false},
        {"high-200-44k", 44100, 20.0, 200.0, 0.4989, TransientShape::HighBurst,
            1, 0.0, true, 0, false},
        {"click-240-48k-accented", 48000, 18.0, 240.0, 0.0743, TransientShape::Click,
            1, 0.0, true, 0, false},
        {"noise-300-48k", 48000, 18.0, 300.0, 0.1511, TransientShape::NoiseBurst,
            1, 0.0, false, 4, false},
        {"low-360-32k", 32000, 16.0, 360.0, 0.0917, TransientShape::LowBurst,
            1, 0.0, false, 0, false},
        {"high-420-48k", 48000, 16.0, 420.0, 0.0529, TransientShape::HighBurst,
            2, 0.18, false, 0, false},
        {"partial-first-bar", 44100, 24.0, 128.0, 1.3471, TransientShape::NoiseBurst,
            1, 0.0, true, 0, false},
        {"long-intro", 48000, 28.0, 111.0, 8.2137, TransientShape::LowBurst,
            2, 0.25, false, 0, false},
    };

    constexpr std::uint32_t sampleRates[] = {32000, 44100, 48000};
    std::uint32_t sweepState = 0xa341316cU;
    const auto nextUnit = [&]() {
        sweepState = sweepState * 1664525U + 1013904223U;
        return static_cast<double>((sweepState >> 8) & 0xffffffU) /
            static_cast<double>(0xffffffU);
    };
    for (std::size_t i = 0; i < 18; ++i) {
        const double bpm = 45.0 + nextUnit() * 380.0;
        const double periodSeconds = 60.0 / bpm;
        const double offset = 0.002 + nextUnit() * std::min(0.9, periodSeconds * 0.92);
        const auto shape = static_cast<TransientShape>(i % 4);
        fixtures.push_back({
            "deterministic-sweep-" + std::to_string(i + 1),
            sampleRates[i % 3],
            bpm < 70.0 ? 28.0 : 18.0,
            bpm,
            offset,
            shape,
            i % 5 == 0 ? 2U : 1U,
            i % 5 == 0 ? 0.21 : 0.0,
            i % 3 == 0,
            i % 7 == 0 ? 6U : 0U,
            i % 8 == 0,
        });
    }

    std::vector<double> candidateErrors;
    std::vector<double> trackerErrors;
    std::vector<Measurement> allMeasurements;
    for (const Fixture& fixture : fixtures) {
        std::vector<Measurement> fixtureMeasurements = measureFixture(fixture);
        for (const Measurement& measurement : fixtureMeasurements) {
            (std::string(measurement.source) == "candidate"
                ? candidateErrors
                : trackerErrors).push_back(measurement.errorMilliseconds);
            allMeasurements.push_back(measurement);
        }
    }

    std::sort(
        allMeasurements.begin(),
        allMeasurements.end(),
        [](const Measurement& left, const Measurement& right) {
            return left.errorMilliseconds > right.errorMilliseconds;
        });
    const double candidateP50 = percentile(candidateErrors, 0.50);
    const double candidateP95 = percentile(candidateErrors, 0.95);
    const double candidateMaximum = *std::max_element(
        candidateErrors.begin(), candidateErrors.end());
    const double trackerP50 = percentile(trackerErrors, 0.50);
    const double trackerP95 = percentile(trackerErrors, 0.95);
    const double trackerMaximum = *std::max_element(
        trackerErrors.begin(), trackerErrors.end());
    std::cout << std::fixed << std::setprecision(3)
        << "synthetic candidate phase: count=" << candidateErrors.size()
        << " p50=" << candidateP50 << "ms"
        << " p95=" << candidateP95 << "ms"
        << " max=" << candidateMaximum << "ms\n"
        << "synthetic tracker phase: count=" << trackerErrors.size()
        << " p50=" << trackerP50 << "ms"
        << " p95=" << trackerP95 << "ms"
        << " max=" << trackerMaximum << "ms\n";
    const std::size_t details = std::min<std::size_t>(8, allMeasurements.size());
    for (std::size_t i = 0; i < details; ++i) {
        const Measurement& measurement = allMeasurements[i];
        std::cout << "  " << measurement.fixture << '/' << measurement.source
            << " center=" << measurement.windowCenterSeconds << "s"
            << " bpm=" << measurement.estimatedBpm
            << " error=" << measurement.errorMilliseconds << "ms\n";
    }

    require(candidateMaximum < 5.0,
        "a synthetic onset candidate exceeded the 5 ms phase target");
    require(trackerMaximum < 5.0,
        "a synthetic tracker point exceeded the 5 ms phase target");
}

double nearestPulseErrorMilliseconds(
    const std::vector<double>& pulses,
    double pulseTimeSeconds)
{
    const auto after = std::lower_bound(
        pulses.begin(), pulses.end(), pulseTimeSeconds);
    double error = std::numeric_limits<double>::infinity();
    if (after != pulses.end()) {
        error = std::abs(*after - pulseTimeSeconds);
    }
    if (after != pulses.begin()) {
        error = std::min(error, std::abs(*(after - 1) - pulseTimeSeconds));
    }
    return error * 1000.0;
}

void testVariableTempoPhaseBaseline()
{
    const std::vector<VariableFixture> fixtures = {
        {"micro-variation", 44100, 40.0, TempoProfile::MicroVariation,
            TransientShape::LowBurst},
        {"rubato", 48000, 48.0, TempoProfile::Rubato,
            TransientShape::NoiseBurst},
        {"tempo-step", 48000, 40.0, TempoProfile::Step,
            TransientShape::Click},
        {"linear-ramp", 32000, 48.0, TempoProfile::LinearRamp,
            TransientShape::NoiseBurst},
        {"phase-reset", 44100, 40.0, TempoProfile::PhaseReset,
            TransientShape::HighBurst},
    };

    std::vector<double> allErrors;
    for (const VariableFixture& fixture : fixtures) {
        const std::vector<double> pulses = makeVariablePulseTimes(fixture);
        const std::vector<float> audio = renderVariableFixture(fixture, pulses);
        const autotiming::AudioView view{
            audio.data(), audio.size(), fixture.sampleRate, 1,
        };
        autotiming::AnalysisOptions options;
        if (fixture.profile != TempoProfile::PhaseReset) {
            options.windowSpecs = {
                {autotiming::WindowScale::Short, 8.0, 4.0},
            };
        }
        options.minimumTempoBpm = 70.0;
        options.maximumTempoBpm = 180.0;
        const autotiming::AnalysisResult result = autotiming::analyze(view, options);

        std::vector<double> errors;
        std::size_t localPhaseAnchors = 0;
        for (const autotiming::TempoTrackPoint& point : result.tempoTrack.points) {
            const bool phaseAnchored =
                point.state == autotiming::TempoTrackState::Observed ||
                point.propagationReason ==
                    autotiming::TempoPropagationReason::LocalPhaseAnchor;
            if (!phaseAnchored ||
                point.timeSeconds < 4.0 ||
                point.timeSeconds > fixture.durationSeconds - 4.0) {
                continue;
            }
            if ((fixture.profile == TempoProfile::Step ||
                    fixture.profile == TempoProfile::PhaseReset) &&
                std::abs(point.timeSeconds - fixture.durationSeconds * 0.5) < 6.0) {
                continue;
            }
            const double expectedBpm = profileBpm(fixture, point.timeSeconds);
            if (std::abs(point.bpm / expectedBpm - 1.0) > 0.08) {
                continue;
            }
            errors.push_back(nearestPulseErrorMilliseconds(
                pulses,
                point.pulseTimeSeconds));
            localPhaseAnchors += point.propagationReason ==
                    autotiming::TempoPropagationReason::LocalPhaseAnchor
                ? 1U
                : 0U;
        }
        if (errors.size() < 4) {
            std::cerr << "variable fixture diagnostics for " << fixture.name << ':\n';
            for (const autotiming::TempoTrackPoint& point : result.tempoTrack.points) {
                std::cerr << "  t=" << point.timeSeconds
                    << " bpm=" << point.bpm
                    << " expected=" << profileBpm(fixture, point.timeSeconds)
                    << " state=" << static_cast<int>(point.state)
                    << " phaseError=" << nearestPulseErrorMilliseconds(
                        pulses,
                        point.pulseTimeSeconds) << "ms\n";
            }
        }
        require(errors.size() >= 4,
            std::string("too few variable-tempo measurements for ") + fixture.name);
        if (fixture.profile == TempoProfile::Rubato) {
            require(localPhaseAnchors >= 4,
                "rubato did not retain local phase anchors through structural tempo carry");
        }
        allErrors.insert(allErrors.end(), errors.begin(), errors.end());
        std::cout << std::fixed << std::setprecision(3)
            << fixture.name << " phase: count=" << errors.size()
            << " p50=" << percentile(errors, 0.50) << "ms"
            << " p95=" << percentile(errors, 0.95) << "ms"
            << " max=" << *std::max_element(errors.begin(), errors.end()) << "ms\n";
        require(*std::max_element(errors.begin(), errors.end()) < 5.0,
            std::string("variable-tempo phase exceeded 5 ms for ") + fixture.name);
    }

    require(*std::max_element(allErrors.begin(), allErrors.end()) < 5.0,
        "a variable-tempo phase observation exceeded the 5 ms target");
}

} // namespace

int main()
{
    try {
        testSyntheticPhaseMatrix();
        testVariableTempoPhaseBaseline();
    } catch (const std::exception& error) {
        std::cerr << "phase accuracy test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
