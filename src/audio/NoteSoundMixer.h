#pragma once

#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>

class NoteSoundMixer
{
public:
    static constexpr int sampleRate = 48000;
    static constexpr int channels = 2;

    void reset()
    {
        m_voiceEnds.fill(0);
        m_nextVoice = 0;
    }

    void mix(float *output, qint64 firstFrame, qint64 frameCount, double adjustedStartFrame,
             double rate, const QVector<double> &timesMs, const float *sample, qint64 sampleFrames,
             float gain)
    {
        const auto outputFrame = [=](double timeMs) {
            // Beat-to-time roundoff can put a cue exactly at a seek point
            // a tiny fraction of a frame below zero.
            constexpr double startToleranceFrames = 1e-6;
            const double offset = timeMs * (sampleRate / 1000.0) - adjustedStartFrame;
            return offset < -startToleranceFrames ? -1LL : std::llround(offset / rate);
        };
        auto note = std::lower_bound(timesMs.cbegin(), timesMs.cend(), firstFrame,
            [&](double timeMs, qint64 frame) { return outputFrame(timeMs) < frame; });
        const qint64 endFrame = firstFrame + frameCount;
        qint64 nextOnset = note != timesMs.cend() ? outputFrame(*note) : endFrame;
        for (qint64 frame = firstFrame; frame < endFrame; ++frame, output += channels)
        {
            if (frame == nextOnset)
            {
                m_voiceEnds[m_nextVoice] = frame + sampleFrames;
                m_nextVoice = (m_nextVoice + 1) % m_voiceEnds.size();
                do
                {
                    ++note;
                    nextOnset = note != timesMs.cend() ? outputFrame(*note) : endFrame;
                } while (nextOnset == frame);
            }

            float cue[channels] = {};
            for (const auto end : m_voiceEnds)
            {
                if (frame >= end)
                    continue;
                const qint64 offset = (sampleFrames - (end - frame)) * channels;
                for (int channel = 0; channel < channels; ++channel)
                    cue[channel] += sample[offset + channel];
            }
            for (int channel = 0; channel < channels; ++channel)
            {
                if (cue[channel] != 0.0f)
                    output[channel] = std::clamp(output[channel] + gain * cue[channel], -1.0f, 1.0f);
            }
        }
    }

private:
    // Keep the same overlap limit as the Qt voice pool. A new attack takes
    // the oldest voice when six cues are still ringing.
    std::array<qint64, 6> m_voiceEnds{};
    size_t m_nextVoice = 0;
};
