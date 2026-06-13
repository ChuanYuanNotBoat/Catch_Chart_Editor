#include "AutoTiming.h"
#include <string>
#include <cstdint>
#include <stdexcept>
#include <algorithm>
#include "util.h"
#include "dsp.h"
#include "fft.h"

// PCM format identifiers corresponding to FMOD sound format values.
enum FmodSoundFormat
{
    kFmodSoundFormatNone = 0,
    kFmodSoundFormatPcm8 = 1,
    kFmodSoundFormatPcm16 = 2,
    kFmodSoundFormatPcm24 = 3,
    kFmodSoundFormatPcm32 = 4,
    kFmodSoundFormatPcmFloat = 5,
};

#define HE_DEBUG_OUTPUT 0

namespace
{

/// FIR filter group delay compensation (ms).
const double kFilterDelay = 1.2;

/// Upper bound for BPM detection (avoids sub-beat aliasing).
const double kMaxBpm = 220.0;

/// Number of subband filters.
const unsigned kSubbandCount = 4;

/// Per-subband weighting for energy accumulation.
const float kSubbandWeights[] = {
    0.3f,
    0.2f,
    0.2f,
    0.3f,
};

/// Filter section layout:
///
/// Sections 0-3:   Subband band-pass filters (LP, BP 600-1200, BP 1200-2400, HP)
///                  Indexed by [section offset within coeff array, number of biquads].
/// Sections 4-7:   HF emphasis filters (only lowest band is non-trivial).
/// Sections 8-9:   LP filter (<300 Hz) for down-sampling pre-filter.
/// Section 10:     Edge-detection positive half (rising edge detection, ~1 kHz).
/// Section 11:     BPM detection band-pass (~4.2–23.8 Hz at 1 kHz).
const unsigned kFilterSections[][2] = {
    // Subband input filters
    {0, 2},   // <600 Hz  low-pass
    {2, 4},   // 600–1200 Hz  band-pass
    {6, 4},   // 1200–2400 Hz  band-pass
    {10, 2},  // >2400 Hz  high-pass
    // Subband HF emphasis filters
    {12, 1},  // Kick drum fast-rise/slow-decay for lowest band (tuned for 44.1 kHz)
    {0, 0},
    {0, 0},
    {0, 0},
    // Pre-decimation low-pass
    {13, 2},  // <300 Hz  low-pass
    // Edge detection filter (positive half)
    {15, 1},  // Rising-edge extractor, ~1 kHz
    // BPM detection filter
    {16, 1},  // ~4.2–23.8 Hz band-pass at 1 kHz
};

/// Approximate group-delay compensation per subband (samples).
const int kSubbandFilterDelay[] = {
    -320,
    -64,
    -32,
    -0,
};

// ---------------------------------------------------------------------------
// Filter coefficient tables (second-order sections, biquad form)
// Each row: [b1, b2, -a1, -a2, gain]
// ---------------------------------------------------------------------------

/// Biquad coefficients for 44100 Hz sample rate.
const double kFilterCoeffSos44[][5] = {
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
    // B1 HF emphasis 44100
    {0, 0, -1.948, 0.9481, 0.048},
    // Butterworth LP 4 300/44100
    {2, 1, -1.9660249635383409, 0.96782223970722425, 0.00044931904222082926},
    {2, 1, -1.9222869522443087, 0.92404424454379519, 0.00043932307487161041},
    // Edge-detection filter 1000
    {0, 0, -1.4, 0.48, 0.2},
    // BPM detection filter 1000
    {0, -1, -1.8799483399273036, 0.88366532316014612, 0.058167338419926939},
};

/// Biquad coefficients for 48000 Hz sample rate.
const double kFilterCoeffSos48[][5] = {
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
    // B1 HF emphasis 48000
    {0, 0, -1.952225, 0.95230941015625, 0.0441},
    // Butterworth LP 4 300/48000
    {2, 1, -1.9688774973857579, 0.97039660175711517, 0.00037977609283935493},
    {2, 1, -1.9285084850826344, 0.92999644239525459, 0.00037198932815510181},
    // Edge-detection filter 1000
    {0, 0, -1.4, 0.48, 0.2},
    // BPM detection filter 1000
    {0, -1, -1.8799483399273036, 0.88366532316014612, 0.058167338419926939},
};

/// Biquad coefficients for 32000 Hz sample rate.
const double kFilterCoeffSos32[][5] = {
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
    // B1 HF emphasis 32000
    {0, 0, -1.9283375, 0.9285274228515625, 0.06615},
    // Butterworth LP 4 300/32000
    {2, 1, -1.9525426196393316, 0.95593497333644439, 0.00084808842427817996},
    {2, 1, -1.8935423413365597, 0.89683218776432150, 0.00082246160694037732},
    // Edge-detection filter 1000
    {0, 0, -1.4, 0.48, 0.2},
    // BPM detection filter 1000
    {0, -1, -1.8799483399273036, 0.88366532316014612, 0.058167338419926939},
};

/// BPM snap table.
///
/// Rows define rational snap targets.  For example, row {2.0, 2.5} means:
///   if the computed BPM lies within 2.5 sigma of the nearest 1/2-multiple,
///   snap it to that multiple.
const double kBpmSnapTable[][2] = {
    {1.0, 3.0},
    {2.0, 2.5},
    {3.0, 2.0},
    {10.0, 2.0},
    {20.0, 1.5},
    {100.0, 1.5},
    {200.0, 1.0},
};

} // namespace

std::once_flag AutoTiming::s_flagFftInit;

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

AutoTiming::Result AutoTiming::detect(const char* buffer, uint32_t size,
                                      int format, int sampleRate, int channel)
{
    // One-time FFT table initialisation.
    std::call_once(s_flagFftInit, initFftTables);

    std::vector<float> audio = decodeFmod(buffer, size, format, channel);
    std::vector<float> feature = preprocess(audio, sampleRate);

    Result ret;
    int bpmRet = calcBpm(feature, ret.rawBpm, ret.rawBpmUncertainty,
                         ret.signature, ret.division);
    if (bpmRet >= 0)
    {
        if (bpmRet < 16)
        {
            ret.bpm = snapBpm(ret.rawBpm, ret.rawBpmUncertainty);
        }
        else
        {
            ret.bpm = ret.rawBpm;
        }
    }
    else
    {
        ret.bpm = 0;
        ret.offset = 0;
        return ret;
    }

    int offsetRet = calcOffset(feature, ret.bpm, ret.offset);
    if (offsetRet < 0)
    {
        ret.offset = 0;
        return ret;
    }

    // Convert offset to "how much to delay audio so beats align to time zero".
    double beatLen = 60000. / ret.bpm;
    ret.offset = beatLen - fmod(ret.offset, beatLen);
    return ret;
}

std::vector<double> AutoTiming::detectOnset(const char* buffer, uint32_t size,
                                            int format, int sampleRate, int channel)
{
    std::call_once(s_flagFftInit, initFftTables);

    const std::vector<float> feature =
        preprocess(decodeFmod(buffer, size, format, channel), sampleRate);

    // Peak detection parameters.
    constexpr std::size_t halfWindowSize = 30;
    constexpr float threshold = 0.5f;

    std::vector<double> result;
    if (feature.size() < halfWindowSize * 2 + 1)
    {
        return result;
    }

    for (std::size_t i = halfWindowSize; i != feature.size() - halfWindowSize; ++i)
    {
        const bool isPeak = [&]
        {
            if (!(feature[i] > threshold))
            {
                return false;
            }

            for (std::size_t j = 1; j <= halfWindowSize; ++j)
            {
                if (!(feature[i - j] < feature[i]) || feature[i] < feature[i + j])
                {
                    return false;
                }
            }

            return true;
        }();

        if (isPeak)
        {
            result.push_back(i / 1000.0);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// PCM decoding
// ---------------------------------------------------------------------------

std::vector<float> AutoTiming::decodeFmod(const char* buffer, uint32_t size,
                                          int format, int channels)
{
    switch (format)
    {
    case kFmodSoundFormatPcm16:
        return convertToMono(buffer, size, SampleFormat::Signed16LE, channels);
    case kFmodSoundFormatPcm24:
        return convertToMono(buffer, size, SampleFormat::Signed24LE, channels);
    case kFmodSoundFormatPcmFloat:
        return convertToMono(buffer, size, SampleFormat::Float32LE, channels);
    default:
        throw std::runtime_error("Unexpected sample format from FMOD");
    }
}

// ---------------------------------------------------------------------------
// Feature extraction
// ---------------------------------------------------------------------------

std::vector<float> AutoTiming::preprocess(const std::vector<float>& audioData,
                                          unsigned sampleRate)
{
    const double (*filterCoeffSos)[5];
    switch (sampleRate)
    {
    case 32000:
        filterCoeffSos = kFilterCoeffSos32;
        break;
    case 44100:
        filterCoeffSos = kFilterCoeffSos44;
        break;
    case 48000:
        filterCoeffSos = kFilterCoeffSos48;
        break;
    default:
        throw std::invalid_argument("Unsupported sample rate");
    }

    // Split into subbands and compute per-band energy envelope.
    size_t len = audioData.size();
    std::vector<float> y(len, 0.0f);
    for (unsigned k = 0; k < kSubbandCount; k++)
    {
        std::vector<float> x(audioData);

        // Subband band-pass filtering.
        filterSos(kFilterSections[k][1],
                  &filterCoeffSos[kFilterSections[k][0]], x);

        // Energy (squared magnitude).
        for (size_t i = 0; i < len; i++)
        {
            x[i] *= x[i];
        }

        // HF emphasis on the lowest subband.
        filterSos(kFilterSections[kSubbandCount + k][1],
                  &filterCoeffSos[kFilterSections[kSubbandCount + k][0]], x);

        // Non-linear compression, delay compensation, weighted accumulation.
        float mul = 2.0f / medianApprox(x, -4);
        compressApprox(x, y, mul, kSubbandWeights[k], kSubbandFilterDelay[k]);
    }

    // Down-sample to 1 kHz.
    filterSos(kFilterSections[kSubbandCount * 2 + 0][1],
              &filterCoeffSos[kFilterSections[kSubbandCount * 2 + 0][0]], y);
    std::vector<float> feature = resample(y, 1000.0 / sampleRate);
    len = feature.size();

    // Odd-symmetry edge detection filter.
    //   y = time-reversed copy of feature
    //   filter positive half on feature, negative half on y (via reversal)
    //   subtract: feature(i) = y_rev[i-1] - feature[i-1]
    y.clear();
    y.resize(len);
    std::copy(feature.rbegin(), feature.rend(), y.begin());

    filterSos(kFilterSections[kSubbandCount * 2 + 1][1],
              &filterCoeffSos[kFilterSections[kSubbandCount * 2 + 1][0]], feature);
    filterSos(kFilterSections[kSubbandCount * 2 + 1][1],
              &filterCoeffSos[kFilterSections[kSubbandCount * 2 + 1][0]], y);

    feature[len - 1] = -feature[len - 2];
    for (int i = len - 2; i > 0; i--)
    {
        feature[i] = y[len - i - 2] - feature[i - 1];
    }
    feature[0] = y[len - 2];

    return feature;
}

// ---------------------------------------------------------------------------
// BPM estimation
// ---------------------------------------------------------------------------

int AutoTiming::calcBpm(const std::vector<float>& feature, double& bpm,
                        double& uncertainty, unsigned& signature,
                        unsigned& division)
{
    bpm = 0.0;
    size_t len = feature.size();

    // Compute auto-correlation of the feature signal.
    //
    // Musical rhythm causes the energy envelope to repeat at beat intervals,
    // so the auto-correlation shows peaks at lags corresponding to beat,
    // half-beat, and bar durations.
    //
    // We limit correlation length to half the signal, or truncate the
    // feature when it would exceed FFT capacity.
    bool tooLong = len + len / 2 > kFftMaxN;
    std::vector<float> r;
    if (tooLong)
    {
        std::vector<float> part(feature.cbegin(),
                                feature.cbegin() + (kFftMaxN * 2 / 3));
        r = autocorr(part, part.size() / 2);
    }
    else
    {
        r = autocorr(feature, len / 2);
    }

    // Find correlation peaks within the first 4000 ms of lag.
    int rlen = std::min(size_t(4000), r.size());
    int plen = rlen;
    std::vector<int> rpeak;
    for (int i = 16; i < rlen - 16; i++)
    {
        if (r[i] > 0.0f)
        {
            bool peak = true;
            for (int j = 1; j < 16; j++)
            {
                if (r[i] < r[i - j] || r[i] < r[i + j])
                {
                    peak = false;
                    break;
                }
            }
            if (peak)
            {
                rpeak.push_back(i);
#if HE_DEBUG_OUTPUT & 1
                printf("%d %f\n", i, r[i]);
#endif
            }
        }
    }

    // Search for harmonic series among peaks (peak at τ, 2τ, 3τ, …).
    // For each candidate fundamental we look for near-integer multiples
    // within ±10 ms.  Running linear regression estimates the fundamental.
    size_t estindex = 0;
    float bestravg = 0.0f;
    std::vector<std::pair<double, double>> ests;
    for (size_t i = 0; i < rpeak.size() && rpeak[i] < plen; i++)
    {
        float bestr = 0.0f;
        // Drop candidates whose correlation is below 0.8× the current best.
        if (ests.empty() || r[rpeak[i]] > bestr * 0.8f)
        {
            if (r[rpeak[i]] > bestr)
            {
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

            // Walk forward at integer multiples; allow ±10 ms tolerance.
            for (double k = 1; p < rlen - 10 && miss <= 0; k++)
            {
                double pest = p;
                for (; j < rpeak.size() && rpeak[j] < p - 10; j++)
                    ;
                if (j < rpeak.size() && rpeak[j] <= p + 10)
                {
                    n++;
                    p = rpeak[j];
                    avgpeak += r[p];
                    pest = peak(p, 1, r[p - 1], r[p], r[p + 1]);
                    sxx += k * k;
                    sxy += k * pest;
                    syy += pest * pest;
                    m = sxy / sxx;
                }
                else
                {
                    miss++;
                }
                p = i32rint(pest + m);
            }

            // Require multiples covering at least the first 2000 ms.
            if (n > 0 && p > rlen / 2)
            {
                avgpeak /= n;
                // Require mean correlation ≥ 0.7× the fundamental peak.
                if (avgpeak > r[rpeak[i]] * 0.70f)
                {
#if HE_DEBUG_OUTPUT & 1
                    printf("%c %d %.3f %f\n",
                           avgpeak > bestravg * 1.25f ? '*' :
                           avgpeak > bestravg * 1.00f ? '+' : ' ',
                           rpeak[i], sxy / sxx, avgpeak);
#endif
                    // Track rising average correlation sequence.
                    // A sharp rise (>1.25×) suggests we crossed from a
                    // subdivision to the true beat period.
                    if (avgpeak > bestravg * 1.00f)
                    {
                        ests.push_back(std::pair<double, double>(sxy / sxx, avgpeak));
                        if (avgpeak > bestravg * 1.25f)
                        {
                            estindex = ests.size() - 1;
                        }
                    }
                    if (avgpeak > bestravg)
                    {
                        bestravg = avgpeak;
                    }
                }
            }
        }
    }

    if (!(bestravg > 0.0f))
    {
        return -1;
    }

    // Clamp to maximum BPM by walking to longer lag estimates.
    double m = ests[estindex].first;
    while (m < 60000.0 / kMaxBpm &&
           estindex < ests.size() - 1 &&
           ests[estindex + 1].first < 60000.0 / kMaxBpm * 2.0)
    {
        bool success = false;
        for (size_t i = estindex + 1;
             i < ests.size() && ests[i].first < 60000.0 / kMaxBpm * 2.0; i++)
        {
            if (fabs(at_remainder(ests[i].first, m)) <= 10.0)
            {
                estindex = i;
                m = ests[estindex].first;
                success = true;
                break;
            }
        }
        if (!success)
        {
            break;
        }
    }

    // Attempt to determine time signature (beats per bar).
    signature = 1;
    for (size_t i = estindex + 1; i < ests.size(); i++)
    {
        if (fabs(at_remainder(ests[i].first, ests[estindex].first)) <= 10.0)
        {
            signature = i32rint(ests[i].first / ests[estindex].first);
            if (signature > 2)
            {
                break;
            }
        }
    }

    // Attempt to determine beat subdivision.
    division = 1;
    for (size_t i = estindex - 1; i != size_t(-1); i--)
    {
        if (fabs(at_remainder(ests[estindex].first, ests[i].first)) <= 10.0)
        {
            division = i32rint(ests[estindex].first / ests[i].first);
            if (division > 2)
            {
                break;
            }
        }
    }

#if HE_DEBUG_OUTPUT & 1
    printf("%d 1/%d\n", signature, division);
#endif

    // Final linear regression across the full auto-correlation range
    // using the selected fundamental period.
    size_t p = i32rint(m);
    unsigned n = 0;
    double sx = 0.0;
    double sxx = 0.0;
    double sxy = 0.0;
    double syy = 0.0;
    unsigned miss = 0;
    unsigned contmiss = 0;
    for (double k = 1.0; p + 10 < r.size() && miss <= 0 && contmiss <= 0; k++)
    {
        size_t maxp = std::max_element(r.begin() + (p - 10),
                                       r.begin() + (p + 11)) - r.begin();
        double pest = p;
        if (r[maxp] > 0.0f && (maxp < p ? p - maxp : maxp - p) <= 8)
        {
            contmiss = 0;
            n++;
            pest = peak(maxp, 1, r[maxp - 1], r[maxp], r[maxp + 1]);
            sx += k;
            sxx += k * k;
            sxy += k * pest;
            syy += pest * pest;
            m = sxy / sxx;
#if HE_DEBUG_OUTPUT & 2
            double bpmt = 60.0 * 1000.0 * sxx / sxy;
            double sigmat = sqrt((syy - sxy * sxy / sxx) / (n - 1));
            double sigmabpmt = sqrt((sxx * syy / sxy / sxy - 1.0) /
                                    (n - 1)) * bpmt;
            double sigmabpmlt = sqrt((sxx * syy / sxy / sxy - 1.0) /
                                     (n - 1) * n) * bpmt;
            printf("%3.0f %9.2f %5.2f %5.2f %.5f %.5f %.2f %.5f %.5f\n",
                   k, pest, pest - k * m, (pest - k * m) / sigmat,
                   m, bpmt, sigmat, sigmabpmt, sigmabpmlt);
#endif
        }
        else
        {
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

    // Multiply sigma by sqrt(N) because auto-correlation peak errors are
    // not independent — the error spectrum approximates f^-2 noise.
    uncertainty = sqrt((sxx * syy / sxy / sxy - 1.0) / (n - 1) * n) * bpm;

#if HE_DEBUG_OUTPUT & 0x10
    printf("%.5f %.5f %d %d %.2f %.5f %.2f %.5f %.5f\n",
           m, sxy / sxx, n, miss, double(p) / len,
           bpm, sigma, sigmabpm, uncertainty);
#endif

    // Sanity checks on result reliability.
    if (n < 4)
    {
        return -1;
    }
    if (sigma > 2.4 || uncertainty / bpm > 0.00005)
    {
        return 16;
    }
    if (miss > 0 || sigma > 0.6)
    {
        return 1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// BPM snapping
// ---------------------------------------------------------------------------

double AutoTiming::snapBpm(double bpm, double uncertainty)
{
    for (size_t i = 0; i < sizeof(kBpmSnapTable) / sizeof(kBpmSnapTable[0]); i++)
    {
#if HE_DEBUG_OUTPUT & 0x20
        printf("%.3f %.2f\n", kBpmSnapTable[i][0],
               at_remainder(bpm, 1.0 / kBpmSnapTable[i][0]) / sigmabpml);
#endif
        if (fabs(at_remainder(bpm, 1.0 / kBpmSnapTable[i][0])) <
            uncertainty * kBpmSnapTable[i][1])
        {
#if HE_DEBUG_OUTPUT & 0x20
            printf("%.5f %.5f\n", bpm,
                   i32rint(bpm * kBpmSnapTable[i][0]) / kBpmSnapTable[i][0]);
#endif
            bpm = i32rint(bpm * kBpmSnapTable[i][0]) / kBpmSnapTable[i][0];
            break;
        }
    }
    return bpm;
}

// ---------------------------------------------------------------------------
// Offset estimation
// ---------------------------------------------------------------------------

int AutoTiming::calcOffset(const std::vector<float>& feature, double bpm,
                           double& offset)
{
    if (!(bpm > 0.0))
    {
        return -1;
    }

    size_t len = feature.size();
    // Stack the feature at beat-length intervals.
    double spb = 60000.0 / bpm;
    size_t slen = static_cast<size_t>(ceil(spb)) + 10;
    std::vector<float> x(slen, 0.0f);
    for (size_t i = 0; i < ceil(len / spb); i++)
    {
        size_t k = i32rint(spb * i);
        for (size_t j = 0; j < slen && j < len - k; j++)
        {
            x[j] += feature[k + j];
        }
    }

    // Simply find the peak in the stacked envelope.
    int maxp = std::max_element(x.begin() + 5, x.end() - 5) - x.begin();
    offset = peak(maxp, 1.0, x[maxp - 1], x[maxp], x[maxp + 1]);

#if HE_DEBUG_OUTPUT & 4
    double rp = peakv(x[maxp - 1], x[maxp], x[maxp + 1]);
    printf("%d %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n",
           maxp, rp, offset,
           x[maxp - 2], x[maxp - 1], x[maxp], x[maxp + 1], x[maxp + 2]);
#endif

    // Compensate for edge-detection filter group delay.
    offset -= kFilterDelay;
    return 0;
}