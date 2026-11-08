#pragma once

#include <QtTypes>
#include <atomic>
#include <memory>

class QString;

// Playback owns the WASAPI stream so its position can be read from the
// endpoint clock. All times here are unadjusted media times.
class WindowsAudioOutput
{
public:
    struct Pcm;
    enum class Status { Waiting, Playing, Ended, Failed };

    static std::shared_ptr<const Pcm> decode(const QString &path,
                                            const std::atomic_bool &cancel);
    explicit WindowsAudioOutput(std::shared_ptr<const Pcm> pcm);
    ~WindowsAudioOutput();

    void setPlayback(double positionMs, double endMs, double rate, bool playing,
                     bool reopenDevice = false);
    void setMuted(bool muted);
    double positionMs(qint64 aheadNs = 0) const;
    Status status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
