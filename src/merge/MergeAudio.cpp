#include "MergeAudio.h"
#include "audio/AudioConverter.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QEventLoop>
#include <QJsonArray>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
// Bound allocations for malformed media and accidental hours-long gaps.
constexpr qint64 MaxFrames = qint64(MergeAudio::SampleRate)*60*30;
void checkSize(qint64 frames)
{
    if (frames < 0 || frames > MaxFrames) throw std::runtime_error("Audio exceeds the 30 minute limit");
}
void checkProgress(const MergeAudio::Progress &progress, float value)
{
    if (progress && !progress(value)) throw std::runtime_error("Cancelled");
}
}

namespace MergeAudio
{
bool decode(const QString &path, QVector<float> &pcm, QString *error, const Progress &progress)
{
    pcm.clear();
    QAudioDecoder decoder;
    QAudioFormat fmt;
    fmt.setSampleRate(SampleRate); fmt.setChannelCount(2); fmt.setSampleFormat(QAudioFormat::Float);
    decoder.setAudioFormat(fmt); decoder.setSource(QUrl::fromLocalFile(path));
    QEventLoop loop;
    bool ok = false, done = false;
    QString failure;
    QTimer poll;
    poll.setInterval(100);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (progress && !progress(decoder.duration() > 0 ? float(decoder.position())/decoder.duration() : 0))
        { failure = QObject::tr("Cancelled"); done = true; decoder.stop(); loop.quit(); }
    });
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        const auto buffer = decoder.read();
        if (!buffer.isValid()) return;
        if (buffer.format().sampleRate() != SampleRate || buffer.format().channelCount() != 2 ||
            buffer.format().sampleFormat() != QAudioFormat::Float || pcm.size()/2+buffer.frameCount() > MaxFrames)
        { failure = QObject::tr("Unsupported audio format or audio longer than 30 minutes."); done = true; decoder.stop(); loop.quit(); return; }
        const auto *data = buffer.constData<float>();
        const qsizetype oldSize = pcm.size();
        pcm.resize(oldSize+buffer.frameCount()*2);
        std::copy(data, data+buffer.frameCount()*2, pcm.begin()+oldSize);
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, [&] { ok = !pcm.isEmpty(); done = true; loop.quit(); });
    QObject::connect(&decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop,
                     [&](auto e) { if (e != QAudioDecoder::NoError) { failure = decoder.errorString(); done = true; loop.quit(); } });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { failure = QObject::tr("Audio decoding timed out."); decoder.stop(); loop.quit(); });
    poll.start(); timeout.start(180000); decoder.start();
    if (!done) loop.exec();
    decoder.stop();
    if (!ok && error) *error = QObject::tr("%1: %2").arg(path, failure.isEmpty() ? QObject::tr("Empty audio") : failure);
    return ok;
}

QVector<float> changeSpeed(const QVector<float> &pcm, double rate, bool changePitch, const Progress &progress)
{
    if (!std::isfinite(rate) || rate < 0.25 || rate > 4 || pcm.size()%2)
        throw std::runtime_error("Invalid audio speed or channel layout");
    if (rate == 1 || pcm.isEmpty()) return pcm;
    const qint64 frames = pcm.size()/2, count = qRound64(frames/rate);
    checkSize(count);
    QVector<float> out(count*2, 0);
    if (changePitch)
    {
        for (qint64 i = 0; i < count; ++i)
        {
            if (i%8192 == 0) checkProgress(progress, float(i)/count);
            const double pos = i*rate;
            const qint64 a = std::min(qint64(pos), frames-1), b = std::min(a+1, frames-1);
            for (int c = 0; c < 2; ++c)
                out[i*2+c] = float(pcm[a*2+c]*(1-(pos-a))+pcm[b*2+c]*(pos-a));
        }
        return out;
    }
    // Waveform-similarity overlap-add. Both stereo channels use the same
    // alignment; correlation chooses a nearby waveform phase before blending.
    constexpr int Hop = 1024, Window = Hop*2, Search = 256;
    qint64 previous = 0;
    for (qint64 dest = 0; dest < count; dest += Hop)
    {
        checkProgress(progress, float(dest)/count);
        const qint64 expected = qRound64(dest*rate);
        qint64 best = std::clamp(expected, qint64(0), std::max(qint64(0), frames-Window));
        if (dest && expected+Window+Search < frames)
        {
            double bestScore = -2;
            for (qint64 candidate = std::max(qint64(0), expected-Search); candidate <= expected+Search; candidate += 4)
            {
                double xy = 0, xx = 1e-12, yy = 1e-12;
                for (int j = 0; j < Hop && dest+j < count; j += 8)
                {
                    const double a = pcm[(previous+Hop+j)*2]+pcm[(previous+Hop+j)*2+1];
                    const double b = pcm[(candidate+j)*2]+pcm[(candidate+j)*2+1];
                    xy += a*b; xx += a*a; yy += b*b;
                }
                const double score = xy/std::sqrt(xx*yy);
                if (score > bestScore) { bestScore = score; best = candidate; }
            }
        }
        for (int j = 0; j < Window && dest+j < count; ++j)
        {
            const qint64 source = std::min(best+j, frames-1);
            const float mix = dest && j < Hop ? float(j)/Hop : 1.0f;
            for (int c = 0; c < 2; ++c)
                out[(dest+j)*2+c] = out[(dest+j)*2+c]*(1-mix)+pcm[source*2+c]*mix;
        }
        previous = best;
    }
    return out;
}

bool render(const QVector<ChartMerger::Segment> &segments, const ChartMerger::Plan &plan,
            const QString &outputPath, QString *error, const Progress &progress)
{
    try
    {
        QVector<float> mix;
        for (int i = 0; i < segments.size(); ++i)
        {
            const auto &seg = segments[i]; const auto &place = plan.placements[i];
            const auto update = [&](float p) { return !progress || progress((i+p)*0.8f/segments.size()); };
            QVector<float> pcm;
            if (!decode(seg.source.audioPath, pcm, error, [&](float p) { return update(p*0.2f); })) return false;
            const auto sourceNotes = seg.source.json["note"].toArray();
            const auto timer = ChartMerger::makeTimer(seg.source.json);
            qint64 prefix = 0;
            for (int n = 0; n < sourceNotes.size(); ++n)
            {
                const auto note = sourceNotes[n].toObject();
                if (!note.contains("sound") || n == seg.source.primarySoundIndex) continue;
                const double seconds = timer(note["beat"].toArray())+
                    note["offset"].toDouble()/1000-seg.source.audioOrigin;
                prefix = std::max(prefix,-qRound64(seconds*SampleRate));
            }
            if (prefix)
            {
                checkSize(pcm.size()/2+prefix);
                QVector<float> padded(pcm.size()+prefix*2,0);
                std::copy(pcm.cbegin(),pcm.cend(),padded.begin()+prefix*2);
                pcm = std::move(padded);
            }
            if (seg.source.primarySoundIndex >= 0)
            {
                const float volume = sourceNotes[seg.source.primarySoundIndex].toObject().value("vol").toDouble(100)/100;
                for (auto &s : pcm) s *= volume;
            }
            for (int n = 0; n < sourceNotes.size(); ++n)
            {
                const auto note = sourceNotes[n].toObject();
                if (!note.contains("sound") || n == seg.source.primarySoundIndex) continue;
                QVector<float> sound;
                if (!decode(ChartMerger::resourcePath(seg.source, note["sound"].toString()), sound, error, update)) return false;
                const double seconds = timer(note["beat"].toArray())+
                                       note["offset"].toDouble()/1000-seg.source.audioOrigin;
                const qint64 start = qRound64(seconds*SampleRate)+prefix;
                const qint64 end = start+sound.size()/2;
                checkSize(std::max(end, qint64(pcm.size()/2)));
                if (end*2 > pcm.size()) pcm.resize(end*2);
                const float gain = note.value("vol").toDouble(100)/100;
                for (qint64 f = std::max(qint64(0), -start); f < sound.size()/2; ++f)
                    for (int c = 0; c < 2; ++c) pcm[(start+f)*2+c] += sound[f*2+c]*gain;
            }
            auto changed = changeSpeed(pcm, seg.rate, seg.changePitch, [&](float p) { return update(0.2f+p*0.6f); });
            pcm.clear(); pcm.squeeze();
            const double audioStart = place.audioStart-double(prefix)/SampleRate/seg.rate;
            const double audioEnd = audioStart+double(changed.size()/2)/SampleRate;
            const double begin = std::max({0.0, audioStart, place.clipStart});
            const double end = std::min(audioEnd, place.clipEnd);
            const double requiredEnd = std::max(place.lastSeconds, end);
            checkSize(qRound64(requiredEnd*SampleRate)+1);
            mix.resize(std::max(qint64(mix.size()), (qRound64(requiredEnd*SampleRate)+1)*2));
            const qint64 first = qRound64(begin*SampleRate), last = qRound64(end*SampleRate);
            const double fadeIn = std::min(seg.fadeIn, std::max(0.0, place.firstSeconds-begin));
            const double fadeOut = std::min(seg.fadeOut, std::max(0.0, end-place.lastSeconds));
            for (qint64 f = first; f < last; ++f)
            {
                if (f%8192 == 0) checkProgress(progress, (i+0.9f)*0.8f/segments.size());
                const double t = double(f)/SampleRate;
                double gain = 1;
                if (fadeIn > 0) gain = std::min(gain, (t-begin)/fadeIn);
                if (fadeOut > 0) gain = std::min(gain, (end-t)/fadeOut);
                const qint64 local = qRound64((t-audioStart)*SampleRate);
                if (local < 0 || local >= changed.size()/2) continue;
                for (int c = 0; c < 2; ++c) mix[f*2+c] += float(gain)*changed[local*2+c];
            }
        }
        // A single common gain prevents clipping without changing balance.
        float peak = 1;
        for (float sample : mix) if (std::isfinite(sample)) peak = std::max(peak, std::abs(sample));
        for (float &sample : mix) sample = std::isfinite(sample) ? sample/peak : 0;
        return AudioConverter::encodePcmToOgg(mix, 2, SampleRate, outputPath, error,
                                             [&](float p) { return !progress || progress(0.8f+p*0.2f); });
    }
    catch (const std::exception &e)
    {
        if (error) *error = QString::fromUtf8(e.what());
        return false;
    }
}
}
