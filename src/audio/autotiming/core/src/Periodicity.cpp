// AutoTiming 2 extension developed from the Malody legacy baseline.
// Provenance and licensing-scope notes: ATTRIBUTION.md.

#include "Periodicity.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

namespace autotiming::detail {
namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr double EnvelopeRate = 100.0;
constexpr double PrecisionEnvelopeRate = 2000.0;

double clamp01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

double linearScore(double value, double zeroAt, double oneAt)
{
    return oneAt > zeroAt
        ? clamp01((value - zeroAt) / (oneAt - zeroAt))
        : (value >= oneAt ? 1.0 : 0.0);
}

double percentile(std::vector<double> values, double fraction)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double position = clamp01(fraction) * static_cast<double>(values.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    const double mix = position - static_cast<double>(lower);
    return values[lower] * (1.0 - mix) + values[upper] * mix;
}

double energyToDb(double energy)
{
    return 10.0 * std::log10(std::max(energy, 1.0e-12));
}

std::vector<double> buildOnsetEnvelope(
    const float* samples,
    std::size_t frameCount,
    std::uint32_t sampleRate)
{
    const std::size_t hopFrames = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::llround(sampleRate / EnvelopeRate)));
    const std::size_t blockCount = (frameCount + hopFrames - 1) / hopFrames;
    if (blockCount < 8) {
        return {};
    }

    std::vector<double> fullDb(blockCount, -120.0);
    std::vector<double> lowDb(blockCount, -120.0);
    std::vector<double> highDb(blockCount, -120.0);

    const double lowPassMemory = std::exp(-2.0 * Pi * 240.0 / sampleRate);
    double lowPass = 0.0;
    double previous = 0.0;
    for (std::size_t block = 0; block < blockCount; ++block) {
        const std::size_t begin = block * hopFrames;
        const std::size_t end = std::min(frameCount, begin + hopFrames);
        double fullEnergy = 0.0;
        double lowEnergy = 0.0;
        double highEnergy = 0.0;
        for (std::size_t frame = begin; frame < end; ++frame) {
            const double sample = std::isfinite(samples[frame]) ? samples[frame] : 0.0;
            lowPass = lowPassMemory * lowPass + (1.0 - lowPassMemory) * sample;
            const double difference = sample - previous;
            previous = sample;
            fullEnergy += sample * sample;
            lowEnergy += lowPass * lowPass;
            highEnergy += difference * difference;
        }
        const double count = static_cast<double>(std::max<std::size_t>(1, end - begin));
        fullDb[block] = energyToDb(fullEnergy / count);
        lowDb[block] = energyToDb(lowEnergy / count);
        highDb[block] = energyToDb(highEnergy / count);
    }

    std::vector<double> flux(blockCount, 0.0);
    for (std::size_t block = 1; block < blockCount; ++block) {
        const double fullRise = std::max(0.0, fullDb[block] - fullDb[block - 1]);
        const double lowRise = std::max(0.0, lowDb[block] - lowDb[block - 1]);
        const double highRise = std::max(0.0, highDb[block] - highDb[block - 1]);
        flux[block] = 0.40 * fullRise + 0.35 * lowRise + 0.25 * highRise;
    }

    const double floor = percentile(flux, 0.50);
    const double upper = percentile(flux, 0.90);
    const double scale = std::max(1.0, upper - floor);
    std::vector<double> envelope(blockCount, 0.0);
    double rollingSum = 0.0;
    constexpr std::size_t History = 8;
    for (std::size_t block = 0; block < blockCount; ++block) {
        const double normalized = std::max(0.0, (flux[block] - floor) / scale);
        const std::size_t historyCount = std::min(block, History);
        const double localMean = historyCount > 0
            ? rollingSum / static_cast<double>(historyCount)
            : 0.0;
        envelope[block] = std::sqrt(std::max(0.0, normalized - 0.20 * localMean));
        rollingSum += normalized;
        if (block >= History) {
            rollingSum -= std::max(0.0, (flux[block - History] - floor) / scale);
        }
    }

    const double envelopeEnergy = std::inner_product(
        envelope.begin(), envelope.end(), envelope.begin(), 0.0);
    if (!(envelopeEnergy > 1.0e-8)) {
        return {};
    }
    return envelope;
}

struct SampledEnvelope {
    std::vector<double> values;
    double rate = 0.0;
};

SampledEnvelope buildPrecisionOnsetEnvelope(
    const float* samples,
    std::size_t frameCount,
    std::uint32_t sampleRate)
{
    const std::size_t hopFrames = std::max<std::size_t>(
        1,
        static_cast<std::size_t>(std::llround(sampleRate / PrecisionEnvelopeRate)));
    const std::size_t blockCount = (frameCount + hopFrames - 1) / hopFrames;
    if (blockCount < 16) {
        return {};
    }

    std::vector<double> powerDb(blockCount, -120.0);
    std::vector<double> peakDb(blockCount, -120.0);
    std::vector<double> differenceDb(blockCount, -120.0);
    double previous = 0.0;
    for (std::size_t block = 0; block < blockCount; ++block) {
        const std::size_t begin = block * hopFrames;
        const std::size_t end = std::min(frameCount, begin + hopFrames);
        double power = 0.0;
        double peak = 0.0;
        double differencePower = 0.0;
        for (std::size_t frame = begin; frame < end; ++frame) {
            const double sample = std::isfinite(samples[frame]) ? samples[frame] : 0.0;
            const double difference = sample - previous;
            previous = sample;
            power += sample * sample;
            peak = std::max(peak, std::abs(sample));
            differencePower += difference * difference;
        }
        const double count = static_cast<double>(std::max<std::size_t>(1, end - begin));
        powerDb[block] = energyToDb(power / count);
        peakDb[block] = 20.0 * std::log10(std::max(peak, 1.0e-6));
        differenceDb[block] = energyToDb(differencePower / count);
    }

    const double referencePeakDb = percentile(peakDb, 0.99);
    std::vector<double> flux(blockCount, 0.0);
    for (std::size_t block = 1; block < blockCount; ++block) {
        const double powerRise = std::max(0.0, powerDb[block] - powerDb[block - 1]);
        const double peakRise = std::max(0.0, peakDb[block] - peakDb[block - 1]);
        const double differenceRise = std::max(
            0.0,
            differenceDb[block] - differenceDb[block - 1]);
        const double relativeAmplitude = clamp01(std::pow(
            10.0,
            (peakDb[block] - referencePeakDb) / 20.0));
        const double salience = 0.20 + 0.80 * std::sqrt(relativeAmplitude);
        flux[block] = salience *
            (0.45 * powerRise + 0.35 * peakRise + 0.20 * differenceRise);
    }

    const double floor = percentile(flux, 0.50);
    const double upper = percentile(flux, 0.95);
    const double scale = std::max(1.0, upper - floor);
    SampledEnvelope envelope;
    envelope.rate = static_cast<double>(sampleRate) / hopFrames;
    envelope.values.resize(blockCount, 0.0);
    for (std::size_t block = 0; block < blockCount; ++block) {
        const double normalized = std::max(0.0, (flux[block] - floor) / scale);
        envelope.values[block] = std::sqrt(normalized);
    }
    return envelope;
}

std::vector<double> normalizedAutocorrelation(
    const std::vector<double>& envelope,
    std::size_t maximumLag)
{
    maximumLag = std::min(maximumLag, envelope.size() - 1);
    std::vector<double> correlation(maximumLag + 1, 0.0);
    for (std::size_t lag = 1; lag <= maximumLag; ++lag) {
        double product = 0.0;
        double leftEnergy = 0.0;
        double rightEnergy = 0.0;
        for (std::size_t i = lag; i < envelope.size(); ++i) {
            const double left = envelope[i];
            const double right = envelope[i - lag];
            product += left * right;
            leftEnergy += left * left;
            rightEnergy += right * right;
        }
        if (leftEnergy > 0.0 && rightEnergy > 0.0) {
            const double support = std::sqrt(
                static_cast<double>(envelope.size() - lag) /
                static_cast<double>(envelope.size()));
            correlation[lag] = clamp01(product / std::sqrt(leftEnergy * rightEnergy)) * support;
        }
    }

    std::vector<double> smoothed(correlation);
    for (std::size_t lag = 2; lag + 2 < correlation.size(); ++lag) {
        smoothed[lag] =
            0.10 * correlation[lag - 2] +
            0.20 * correlation[lag - 1] +
            0.40 * correlation[lag] +
            0.20 * correlation[lag + 1] +
            0.10 * correlation[lag + 2];
    }
    return smoothed;
}

std::vector<double> combScores(
    const std::vector<double>& correlation,
    std::size_t minimumLag,
    std::size_t maximumLag)
{
    std::vector<double> score(correlation.size(), 0.0);
    constexpr double Weights[] = {0.62, 0.22, 0.11, 0.05};
    for (std::size_t lag = minimumLag; lag <= maximumLag; ++lag) {
        double weighted = 0.0;
        double weightSum = 0.0;
        for (std::size_t harmonic = 1; harmonic <= 4; ++harmonic) {
            const std::size_t harmonicLag = lag * harmonic;
            if (harmonicLag >= correlation.size()) {
                break;
            }
            const double weight = Weights[harmonic - 1];
            weighted += weight * correlation[harmonicLag];
            weightSum += weight;
        }
        score[lag] = weightSum > 0.0 ? weighted / weightSum : 0.0;
    }
    return score;
}

double refinePeakLag(const std::vector<double>& score, std::size_t lag)
{
    if (lag == 0 || lag + 1 >= score.size()) {
        return static_cast<double>(lag);
    }
    const double left = score[lag - 1];
    const double center = score[lag];
    const double right = score[lag + 1];
    const double denominator = left - 2.0 * center + right;
    if (std::abs(denominator) < 1.0e-12) {
        return static_cast<double>(lag);
    }
    const double offset = std::max(-1.0, std::min(1.0, 0.5 * (left - right) / denominator));
    return static_cast<double>(lag) + offset;
}

std::pair<double, double> estimatePhase(
    const std::vector<double>& envelope,
    double periodBins,
    double envelopeRate = EnvelopeRate)
{
    const std::size_t phaseBins = std::max<std::size_t>(
        2,
        static_cast<std::size_t>(std::ceil(periodBins)));
    std::vector<double> folded(phaseBins, 0.0);
    double total = 0.0;
    for (std::size_t i = 0; i < envelope.size(); ++i) {
        const double value = envelope[i];
        const double phase = std::fmod(static_cast<double>(i), periodBins);
        const double position = phase / periodBins * phaseBins;
        const std::size_t lower = static_cast<std::size_t>(std::floor(position)) % phaseBins;
        const std::size_t upper = (lower + 1) % phaseBins;
        const double mix = position - std::floor(position);
        folded[lower] += value * (1.0 - mix);
        folded[upper] += value * mix;
        total += value;
    }

    const auto maximum = std::max_element(folded.begin(), folded.end());
    if (maximum == folded.end() || !(total > 0.0)) {
        return {0.0, 0.0};
    }
    const std::size_t index = static_cast<std::size_t>(maximum - folded.begin());
    const std::size_t leftIndex = (index + phaseBins - 1) % phaseBins;
    const std::size_t rightIndex = (index + 1) % phaseBins;
    const double denominator =
        folded[leftIndex] - 2.0 * folded[index] + folded[rightIndex];
    const double peakOffset = std::abs(denominator) > 1.0e-12
        ? std::max(-1.0, std::min(
            1.0,
            0.5 * (folded[leftIndex] - folded[rightIndex]) / denominator))
        : 0.0;
    const double mean = total / static_cast<double>(phaseBins);
    const double concentration = clamp01((*maximum - mean) / std::max(*maximum, 1.0e-12));
    double phasePosition = static_cast<double>(index) + peakOffset;
    if (phasePosition < 0.0) {
        phasePosition += static_cast<double>(phaseBins);
    }
    if (phasePosition >= static_cast<double>(phaseBins)) {
        phasePosition -= static_cast<double>(phaseBins);
    }
    return {
        phasePosition / static_cast<double>(phaseBins) * periodBins / envelopeRate,
        concentration,
    };
}

double normalizedCorrelationAt(
    const std::vector<double>& envelope,
    std::size_t lag)
{
    if (lag == 0 || lag >= envelope.size()) {
        return 0.0;
    }
    double product = 0.0;
    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    for (std::size_t i = lag; i < envelope.size(); ++i) {
        const double left = envelope[i];
        const double right = envelope[i - lag];
        product += left * right;
        leftEnergy += left * left;
        rightEnergy += right * right;
    }
    if (!(leftEnergy > 0.0) || !(rightEnergy > 0.0)) {
        return 0.0;
    }
    const double support = std::sqrt(
        static_cast<double>(envelope.size() - lag) /
        static_cast<double>(envelope.size()));
    return clamp01(product / std::sqrt(leftEnergy * rightEnergy)) * support;
}

double localCombScore(const std::vector<double>& envelope, std::size_t lag)
{
    constexpr double Weights[] = {0.68, 0.20, 0.08, 0.04};
    double weighted = 0.0;
    double weightSum = 0.0;
    for (std::size_t harmonic = 1; harmonic <= 4; ++harmonic) {
        const std::size_t harmonicLag = lag * harmonic;
        if (harmonicLag >= envelope.size()) {
            break;
        }
        weighted += Weights[harmonic - 1] *
            normalizedCorrelationAt(envelope, harmonicLag);
        weightSum += Weights[harmonic - 1];
    }
    return weightSum > 0.0 ? weighted / weightSum : 0.0;
}

struct LagEstimate {
    double periodBins = 0.0;
    double score = 0.0;
};

LagEstimate refineLocalPeriod(
    const std::vector<double>& envelope,
    double coarsePeriodBins,
    double searchRadius)
{
    LagEstimate estimate;
    if (envelope.size() < 16 || !(coarsePeriodBins > 0.0)) {
        return estimate;
    }
    const std::size_t minimumLag = std::max<std::size_t>(
        2,
        static_cast<std::size_t>(std::floor(
            coarsePeriodBins * (1.0 - searchRadius))));
    const std::size_t maximumLag = std::min<std::size_t>(
        envelope.size() - 2,
        static_cast<std::size_t>(std::ceil(
            coarsePeriodBins * (1.0 + searchRadius))));
    if (maximumLag <= minimumLag + 2) {
        return estimate;
    }

    std::vector<double> scores(maximumLag - minimumLag + 1, 0.0);
    for (std::size_t lag = minimumLag; lag <= maximumLag; ++lag) {
        scores[lag - minimumLag] = localCombScore(envelope, lag);
    }
    const auto maximum = std::max_element(scores.begin(), scores.end());
    if (maximum == scores.end() || !(*maximum > 0.0)) {
        return estimate;
    }

    const std::size_t scoreIndex = static_cast<std::size_t>(maximum - scores.begin());
    const std::size_t bestLag = minimumLag + scoreIndex;
    estimate.periodBins = static_cast<double>(bestLag);
    estimate.score = *maximum;
    if (scoreIndex > 0 && scoreIndex + 1 < scores.size()) {
        const double left = scores[scoreIndex - 1];
        const double center = scores[scoreIndex];
        const double right = scores[scoreIndex + 1];
        const double denominator = left - 2.0 * center + right;
        if (std::abs(denominator) > 1.0e-12) {
            estimate.periodBins += std::max(-1.0, std::min(
                1.0,
                0.5 * (left - right) / denominator));
        }
    }
    return estimate;
}

struct PrecisionEstimate {
    double bpm = 0.0;
    double periodBins = 0.0;
    double phaseSeconds = 0.0;
    double phaseConfidence = 0.0;
    double stationarity = 0.0;
    double localPulseSeconds = 0.0;
    double localPulseConfidence = 0.0;
};

struct PulseGridFit {
    double periodBins = 0.0;
    double phaseSeconds = 0.0;
    double residualSeconds = 0.0;
    std::size_t support = 0;
};

struct LocalPulseAnchor {
    double seconds = 0.0;
    double confidence = 0.0;
};

LocalPulseAnchor findLocalPulseAnchor(
    const SampledEnvelope& envelope,
    double periodBins,
    double phaseSeconds)
{
    LocalPulseAnchor anchor;
    if (envelope.values.empty() || !(envelope.rate > 0.0) || !(periodBins > 2.0)) {
        return anchor;
    }
    const double center = static_cast<double>(envelope.values.size() - 1) * 0.5;
    const double phaseBins = phaseSeconds * envelope.rate;
    const double predicted = phaseBins +
        std::round((center - phaseBins) / periodBins) * periodBins;
    const std::size_t searchRadius = std::max<std::size_t>(
        2,
        static_cast<std::size_t>(std::llround(std::min(
            periodBins * 0.18,
            envelope.rate * 0.035))));
    const std::size_t predictedIndex = static_cast<std::size_t>(std::llround(
        std::max(0.0, std::min(
            predicted,
            static_cast<double>(envelope.values.size() - 1)))));
    const std::size_t begin = predictedIndex > searchRadius
        ? predictedIndex - searchRadius
        : 0;
    const std::size_t end = std::min(
        envelope.values.size() - 1,
        predictedIndex + searchRadius);
    std::size_t peakIndex = begin;
    for (std::size_t i = begin + 1; i <= end; ++i) {
        if (envelope.values[i] > envelope.values[peakIndex]) {
            peakIndex = i;
        }
    }

    const double globalPeak = *std::max_element(
        envelope.values.begin(), envelope.values.end());
    if (!(globalPeak > 0.0) || envelope.values[peakIndex] < globalPeak * 0.12) {
        return anchor;
    }
    double peakPosition = static_cast<double>(peakIndex);
    if (peakIndex > 0 && peakIndex + 1 < envelope.values.size()) {
        const double left = envelope.values[peakIndex - 1];
        const double centerValue = envelope.values[peakIndex];
        const double right = envelope.values[peakIndex + 1];
        const double denominator = left - 2.0 * centerValue + right;
        if (std::abs(denominator) > 1.0e-12) {
            peakPosition += std::max(-1.0, std::min(
                1.0,
                0.5 * (left - right) / denominator));
        }
    }
    anchor.seconds = peakPosition / envelope.rate;
    anchor.confidence = clamp01(envelope.values[peakIndex] / globalPeak);
    return anchor;
}

PulseGridFit fitPulseGrid(
    const SampledEnvelope& envelope,
    double initialPeriodBins,
    double initialPhaseSeconds)
{
    PulseGridFit fit;
    if (envelope.values.empty() || !(envelope.rate > 0.0) ||
        !(initialPeriodBins > 2.0)) {
        return fit;
    }

    const double maximumValue = *std::max_element(
        envelope.values.begin(), envelope.values.end());
    if (!(maximumValue > 0.0)) {
        return fit;
    }
    const std::size_t searchRadius = std::max<std::size_t>(
        2,
        static_cast<std::size_t>(std::llround(std::min(
            initialPeriodBins * 0.12,
            envelope.rate * 0.020))));
    const double initialPhaseBins = initialPhaseSeconds * envelope.rate;

    double sumWeight = 0.0;
    double sumIndex = 0.0;
    double sumTime = 0.0;
    double sumIndexSquared = 0.0;
    double sumIndexTime = 0.0;
    std::vector<std::pair<double, double>> observations;
    for (std::size_t pulseIndex = 0;; ++pulseIndex) {
        const double predicted =
            initialPhaseBins + static_cast<double>(pulseIndex) * initialPeriodBins;
        if (predicted >= static_cast<double>(envelope.values.size())) {
            break;
        }
        const std::size_t center = static_cast<std::size_t>(std::llround(predicted));
        const std::size_t begin = center > searchRadius ? center - searchRadius : 0;
        const std::size_t end = std::min(
            envelope.values.size() - 1,
            center + searchRadius);
        std::size_t peakIndex = begin;
        for (std::size_t i = begin + 1; i <= end; ++i) {
            if (envelope.values[i] > envelope.values[peakIndex]) {
                peakIndex = i;
            }
        }
        const double peakValue = envelope.values[peakIndex];
        if (peakValue < maximumValue * 0.12) {
            continue;
        }

        double peakPosition = static_cast<double>(peakIndex);
        if (peakIndex > 0 && peakIndex + 1 < envelope.values.size()) {
            const double left = envelope.values[peakIndex - 1];
            const double centerValue = envelope.values[peakIndex];
            const double right = envelope.values[peakIndex + 1];
            const double denominator = left - 2.0 * centerValue + right;
            if (std::abs(denominator) > 1.0e-12) {
                peakPosition += std::max(-1.0, std::min(
                    1.0,
                    0.5 * (left - right) / denominator));
            }
        }

        const double weight = std::max(0.05, peakValue);
        const double index = static_cast<double>(pulseIndex);
        sumWeight += weight;
        sumIndex += weight * index;
        sumTime += weight * peakPosition;
        sumIndexSquared += weight * index * index;
        sumIndexTime += weight * index * peakPosition;
        observations.push_back({index, peakPosition});
    }
    if (observations.size() < 4 || !(sumWeight > 0.0)) {
        return fit;
    }

    const double denominator =
        sumWeight * sumIndexSquared - sumIndex * sumIndex;
    if (!(denominator > 1.0e-9)) {
        return fit;
    }
    const double periodBins =
        (sumWeight * sumIndexTime - sumIndex * sumTime) / denominator;
    if (!(periodBins > initialPeriodBins * 0.97) ||
        !(periodBins < initialPeriodBins * 1.03)) {
        return fit;
    }
    const double interceptBins = (sumTime - periodBins * sumIndex) / sumWeight;
    double squaredResidual = 0.0;
    for (const auto& observation : observations) {
        const double residual = observation.second -
            (interceptBins + periodBins * observation.first);
        squaredResidual += residual * residual;
    }

    double phaseBins = std::fmod(interceptBins, periodBins);
    if (phaseBins < 0.0) {
        phaseBins += periodBins;
    }
    fit.periodBins = periodBins;
    fit.phaseSeconds = phaseBins / envelope.rate;
    fit.residualSeconds = std::sqrt(
        squaredResidual / static_cast<double>(observations.size())) /
        envelope.rate;
    fit.support = observations.size();
    return fit;
}

PrecisionEstimate refinePeriodAndPhase(
    const SampledEnvelope& envelope,
    double coarseBpm)
{
    PrecisionEstimate refined;
    if (envelope.values.empty() || !(envelope.rate > 0.0) || !(coarseBpm > 0.0)) {
        return refined;
    }

    const double coarsePeriodBins = envelope.rate * 60.0 / coarseBpm;
    const LagEstimate fullPeriod = refineLocalPeriod(
        envelope.values,
        coarsePeriodBins,
        0.06);
    if (!(fullPeriod.periodBins > 0.0)) {
        return refined;
    }
    double periodBins = fullPeriod.periodBins;

    auto phase = estimatePhase(
        envelope.values,
        periodBins,
        envelope.rate);
    const PulseGridFit gridFit = fitPulseGrid(
        envelope,
        periodBins,
        phase.first);
    if (gridFit.support >= 4 &&
        gridFit.residualSeconds <= std::min(0.008, periodBins / envelope.rate * 0.04)) {
        periodBins = gridFit.periodBins;
        phase.first = gridFit.phaseSeconds;
    }
    const LocalPulseAnchor localAnchor = findLocalPulseAnchor(
        envelope,
        periodBins,
        phase.first);
    refined.bpm = 60.0 * envelope.rate / periodBins;
    refined.periodBins = periodBins;
    refined.phaseSeconds = phase.first;
    refined.phaseConfidence = phase.second;
    refined.localPulseSeconds = localAnchor.seconds;
    refined.localPulseConfidence = localAnchor.confidence;

    const std::size_t split = envelope.values.size() / 2;
    if (split >= 16 && envelope.values.size() - split >= 16) {
        const std::vector<double> firstHalf(
            envelope.values.begin(),
            envelope.values.begin() + static_cast<std::ptrdiff_t>(split));
        const std::vector<double> secondHalf(
            envelope.values.begin() + static_cast<std::ptrdiff_t>(split),
            envelope.values.end());
        const LagEstimate firstPeriod = refineLocalPeriod(
            firstHalf,
            periodBins,
            0.12);
        const LagEstimate secondPeriod = refineLocalPeriod(
            secondHalf,
            periodBins,
            0.12);
        const auto firstPhase = estimatePhase(firstHalf, periodBins, envelope.rate);
        const auto secondPhase = estimatePhase(secondHalf, periodBins, envelope.rate);
        const double periodSeconds = periodBins / envelope.rate;
        const double secondAbsolutePhase =
            static_cast<double>(split) / envelope.rate + secondPhase.first;
        const double phaseDrift = std::abs(std::remainder(
            secondAbsolutePhase - firstPhase.first,
            periodSeconds));
        const double driftInBeats = phaseDrift / periodSeconds;
        const double driftScore = clamp01(1.0 - (driftInBeats - 0.01) / 0.07);
        if (firstPeriod.periodBins > 0.0 && secondPeriod.periodBins > 0.0) {
            const double tempoDriftOctaves = std::abs(std::log2(
                secondPeriod.periodBins / firstPeriod.periodBins));
            const double tempoScore = clamp01(
                1.0 - (tempoDriftOctaves - 0.004) / 0.025);
            refined.stationarity = std::min(driftScore, tempoScore);
        }
    }
    return refined;
}

struct PeakCandidate {
    std::size_t lag = 0;
    double score = 0.0;
};

} // namespace

std::vector<PeriodicityEstimate> estimateOnsetPeriodicity(
    const float* monoSamples,
    std::size_t frameCount,
    std::uint32_t sampleRate,
    double minimumBpm,
    double maximumBpm,
    double minimumScore,
    std::size_t maximumCandidates)
{
    if (monoSamples == nullptr || frameCount == 0 || sampleRate == 0 ||
        !(minimumBpm > 0.0) || !(maximumBpm > minimumBpm) || maximumCandidates == 0) {
        return {};
    }

    const std::vector<double> envelope = buildOnsetEnvelope(
        monoSamples,
        frameCount,
        sampleRate);
    if (envelope.empty()) {
        return {};
    }
    const SampledEnvelope precisionEnvelope = buildPrecisionOnsetEnvelope(
        monoSamples,
        frameCount,
        sampleRate);

    const std::size_t minimumLag = std::max<std::size_t>(
        2,
        static_cast<std::size_t>(std::floor(EnvelopeRate * 60.0 / maximumBpm)));
    const std::size_t maximumLag = std::min<std::size_t>(
        envelope.size() - 2,
        static_cast<std::size_t>(std::ceil(EnvelopeRate * 60.0 / minimumBpm)));
    if (maximumLag <= minimumLag + 2) {
        return {};
    }

    const std::vector<double> correlation = normalizedAutocorrelation(
        envelope,
        maximumLag * 4);
    const std::vector<double> scores = combScores(correlation, minimumLag, maximumLag);
    std::vector<double> activeScores(
        scores.begin() + static_cast<std::ptrdiff_t>(minimumLag),
        scores.begin() + static_cast<std::ptrdiff_t>(maximumLag + 1));
    const double scoreFloor = percentile(activeScores, 0.50);

    std::vector<PeakCandidate> peaks;
    for (std::size_t lag = minimumLag + 2; lag + 2 <= maximumLag; ++lag) {
        if (scores[lag] < scores[lag - 1] || scores[lag] < scores[lag + 1] ||
            scores[lag] < scores[lag - 2] || scores[lag] < scores[lag + 2]) {
            continue;
        }
        const double shoulder = std::max(scores[lag - 2], scores[lag + 2]);
        const double absoluteStrength = linearScore(scores[lag], 0.06, 0.55);
        const double contrast = linearScore(scores[lag] - scoreFloor, 0.015, 0.25);
        const double prominence = linearScore(scores[lag] - shoulder, 0.001, 0.06);
        const double candidateScore = clamp01(
            0.55 * absoluteStrength + 0.30 * contrast + 0.15 * prominence);
        if (candidateScore >= minimumScore) {
            peaks.push_back({lag, candidateScore});
        }
    }
    std::sort(peaks.begin(), peaks.end(), [](const PeakCandidate& left, const PeakCandidate& right) {
        return left.score > right.score;
    });

    std::vector<PeriodicityEstimate> estimates;
    for (const PeakCandidate& peak : peaks) {
        const double refinedLag = refinePeakLag(scores, peak.lag);
        const double bpm = 60.0 * EnvelopeRate / refinedLag;
        const bool duplicate = std::any_of(
            estimates.begin(),
            estimates.end(),
            [=](const PeriodicityEstimate& value) {
                return std::abs(value.bpm / bpm - 1.0) < 0.015;
            });
        if (duplicate || bpm < minimumBpm || bpm > maximumBpm) {
            continue;
        }

        const auto coarsePhase = estimatePhase(envelope, refinedLag);
        const PrecisionEstimate precision = refinePeriodAndPhase(
            precisionEnvelope,
            bpm);
        const bool hasPrecisionPhase = precision.bpm > 0.0 &&
            precision.phaseConfidence > 0.0;
        const bool hasStablePrecision = hasPrecisionPhase &&
            precision.stationarity >= 0.55;
        const double finalBpm = hasStablePrecision ? precision.bpm : bpm;
        const bool hasLocalPulse = !hasStablePrecision &&
            precision.localPulseConfidence > 0.0;
        const auto phase = hasStablePrecision
            ? std::make_pair(
                precision.phaseSeconds,
                precision.phaseConfidence)
            : hasLocalPulse
                ? std::make_pair(
                    precision.localPulseSeconds,
                    precision.phaseConfidence * precision.localPulseConfidence * 0.35)
            : coarsePhase;
        const double scoringPhaseConfidence = hasStablePrecision
            ? precision.phaseConfidence
            : coarsePhase.second;
        PeriodicityEstimate estimate;
        estimate.bpm = finalBpm;
        estimate.score = clamp01(
            peak.score * (0.75 + 0.25 * scoringPhaseConfidence));
        estimate.uncertaintyBpm = hasStablePrecision
            ? std::max(0.005, finalBpm / std::max(2.0, precision.periodBins))
            : std::max(0.01, bpm / refinedLag);
        estimate.phaseSeconds = phase.first;
        estimate.phaseConfidence = phase.second;
        estimates.push_back(estimate);
        if (estimates.size() >= maximumCandidates) {
            break;
        }
    }
    return estimates;
}

} // namespace autotiming::detail
