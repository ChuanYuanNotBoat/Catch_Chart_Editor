// Manual diagnostic: observe the real editor playback chain and Windows output.
// Uses temporary synthetic WAVs and isolated settings; never writes chart files.
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include "audio/AudioPlayer.h"
#include "audio/NoteSoundPlayer.h"
#include "controller/ChartController.h"
#include "controller/PlaybackController.h"
#include "model/Chart.h"
#include "ui/CustomWidgets/ChartCanvas/ChartCanvas.h"
#include "utils/MathUtils.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaDevices>
#include <QPlaybackOptions>
#include <QAudioDevice>
#include <QAudioBufferOutput>
#include <QAudioBuffer>
#include <QSettings>
#include <QSoundEffect>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <chrono>
#include <cmath>
#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace {
double steadyMs()
{
    return std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool writePulses(const QString &path, bool effect, double playbackRate, int intervalMs)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    constexpr int rate = 48000;
    const int frames = effect ? rate / 10 : rate * 40;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + frames * 4);
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(2) << quint32(rate)
        << quint32(rate * 4) << quint16(4) << quint16(16);
    out.writeRawData("data", 4);
    out << quint32(frames * 4);
    for (int i = 0; i < frames; ++i) {
        const int pulseFrame = effect ? i : (i - 6576) % (rate * intervalMs / 1000);
        const bool active = (effect || i >= 6576) && pulseFrame < (effect ? frames : rate / 50);
        const auto sample = qint16(active ? 6000 * std::sin(2 * 3.141592653589793 *
            (effect ? 1760 : 440 / playbackRate) * pulseFrame / rate) : 0);
        out << qint16(effect ? 0 : sample) << qint16(effect ? sample : 0);
    }
    return out.status() == QDataStream::Ok;
}

class Loopback {
public:
    ComPtr<IAudioClient> audio;
    ComPtr<IAudioCaptureClient> capture;
    WAVEFORMATEX *format = nullptr;
    QJsonArray music, hits;
    double lastOnset[2] = {-1e9, -1e9};
    bool floating = false;
    double musicFrequency = 440;
    double coefficient[2] = {}, state1[2] = {}, state2[2] = {};
    bool toneActive[2] = {};
    int windowFrames = 0, windowCount = 0;
    double windowStartMs = 0, energy = 0;
    QString error;
    ~Loopback() { if (audio) audio->Stop(); CoTaskMemFree(format); }
    bool start()
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
            CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(result)) result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (SUCCEEDED(result)) result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audio);
        if (SUCCEEDED(result)) result = audio->GetMixFormat(&format);
        if (SUCCEEDED(result)) {
            floating = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                 IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE *>(format)->SubFormat,
                             KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
            if (format->nChannels < 2 || (floating ? format->wBitsPerSample != 32 : format->wBitsPerSample != 16))
                result = E_NOTIMPL;
        }
        if (SUCCEEDED(result)) result = audio->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000, 0, format, nullptr);
        if (SUCCEEDED(result)) result = audio->GetService(IID_PPV_ARGS(&capture));
        if (SUCCEEDED(result)) result = audio->Start();
        if (SUCCEEDED(result)) {
            windowFrames = format->nSamplesPerSec / 100;
            coefficient[0] = 2 * std::cos(2 * 3.141592653589793 * musicFrequency / format->nSamplesPerSec);
            coefficient[1] = 2 * std::cos(2 * 3.141592653589793 * 1760 / format->nSamplesPerSec);
        }
        if (FAILED(result)) error = QString("WASAPI HRESULT 0x%1").arg(quint32(result), 8, 16, QLatin1Char('0'));
        return SUCCEEDED(result);
    }
    void poll()
    {
        if (!capture) return;
        UINT32 pending = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&pending)) && pending) {
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            UINT64 devicePosition = 0, qpc100ns = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, &devicePosition, &qpc100ns))) break;
            if (!(flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)) {
                for (UINT32 frame = 0; frame < frames; ++frame) {
                    const double timeMs = double(qpc100ns) / 10000.0 + 1000.0 * frame / format->nSamplesPerSec;
                    double mono = 0;
                    for (int channel = 0; channel < format->nChannels; ++channel) {
                        const int index = frame * format->nChannels + channel;
                        const double value = (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 0.0 :
                            (floating ? reinterpret_cast<float *>(data)[index] :
                             reinterpret_cast<qint16 *>(data)[index] / 32768.0);
                        mono += value / format->nChannels;
                    }
                    if (!windowCount) windowStartMs = timeMs;
                    energy += mono * mono;
                    for (int tone = 0; tone < 2; ++tone) {
                        const double next = mono + coefficient[tone] * state1[tone] - state2[tone];
                        state2[tone] = state1[tone]; state1[tone] = next;
                    }
                    if (++windowCount == windowFrames) {
                        for (int tone = 0; tone < 2; ++tone) {
                            const double power = state1[tone] * state1[tone] + state2[tone] * state2[tone] -
                                coefficient[tone] * state1[tone] * state2[tone];
                            const bool active = 4 * power / (windowFrames * windowFrames) > 0.0001 &&
                                2 * power > 0.15 * windowFrames * energy;
                            if (active && !toneActive[tone] && windowStartMs - lastOnset[tone] > 100) {
                                (tone == 0 ? music : hits).append(windowStartMs);
                                lastOnset[tone] = windowStartMs;
                            }
                            toneActive[tone] = active;
                            state1[tone] = state2[tone] = 0;
                        }
                        windowCount = 0; energy = 0;
                    }
                }
            }
            capture->ReleaseBuffer(frames);
        }
    }
};
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({QCommandLineOption("output", "JSON result path", "path"),
                       QCommandLineOption("speed", "Playback rate", "rate", "1"),
                       QCommandLineOption("duration-ms", "Wall duration", "ms", "4500"),
                       QCommandLineOption("chart", "Read an existing chart and its audio without changing files", "path"),
                       QCommandLineOption("start-ms", "Start position on the media timeline", "ms", "0"),
                       QCommandLineOption("paint-delay-ms", "Simulated paint acknowledgement delay", "ms", "0"),
                       QCommandLineOption("stall-ms", "UI stall at two seconds", "ms", "0")});
    parser.addOption(QCommandLineOption("interval-ms", "Synthetic note interval on the media timeline", "ms", "437"));
    parser.addOption(QCommandLineOption("hit-file", "Use an existing sound effect without changing it", "path"));
    parser.addOption(QCommandLineOption("muted", "Silence only this diagnostic player's output"));
    parser.addOption(QCommandLineOption("no-loopback", "Disable output capture"));
    parser.addOption(QCommandLineOption("file-playback", "Override the editor's streaming intent for the A/B experiment"));
    parser.addOption(QCommandLineOption("streaming", "Reproduce the previous LowLatencyStreaming configuration"));
    parser.process(app);
    if (!temporary.isValid() || !parser.isSet("output")) return 2;
    QString musicPath = temporary.filePath("music.wav");
    const QString hitPath = parser.isSet("hit-file") ? QFileInfo(parser.value("hit-file")).absoluteFilePath() :
        temporary.filePath("hit.wav");
    const double requestedRate = parser.value("speed").toDouble();
    const int intervalMs = parser.value("interval-ms").toInt();
    if (!std::isfinite(requestedRate) || requestedRate < .1 || requestedRate > 10) return 2;
    if (intervalMs < 1 || intervalMs > 5000) return 2;
    if (!writePulses(musicPath, false, requestedRate, intervalMs) ||
        (!parser.isSet("hit-file") && !writePulses(hitPath, true, requestedRate, intervalMs))) return 3;
    if (!QFileInfo::exists(hitPath)) return 3;
    double startMs = 0;
    AudioPlayer audio;
    auto *decodedOutput = audio.findChild<QAudioBufferOutput *>();
    if (!decodedOutput) {
        decodedOutput = new QAudioBufferOutput(&audio);
        audio.findChild<QMediaPlayer *>()->setAudioBufferOutput(decodedOutput);
    }
    QJsonArray decodedBuffers;
    QObject::connect(decodedOutput, &QAudioBufferOutput::audioBufferReceived, &app,
        [&](const QAudioBuffer &buffer) {
            if (buffer.isValid()) decodedBuffers.append(QJsonObject{
                {"start_ms", double(buffer.startTime()) / 1000},
                {"wall_ms", steadyMs() - startMs},
                {"duration_ms", double(buffer.duration()) / 1000}});
        });
    if (parser.isSet("file-playback") || parser.isSet("streaming")) {
        auto *media = audio.findChild<QMediaPlayer *>();
        QPlaybackOptions options = media->playbackOptions();
        options.setPlaybackIntent(parser.isSet("streaming") ?
            QPlaybackOptions::PlaybackIntent::LowLatencyStreaming : QPlaybackOptions::PlaybackIntent::Playback);
        media->setPlaybackOptions(options);
    }
    audio.setAudioCorrectionEnabled(false);
    if (parser.isSet("muted")) audio.findChild<QAudioOutput *>()->setMuted(true);
    PlaybackController playback(&audio);
    playback.setDisplayRefreshRate(60);
    playback.setFrameRateCap(60);
    playback.setSpeed(parser.value("speed").toDouble());
    Chart chart;
    chart.bpmList().clear();
    chart.addBpm(BpmEntry(0, 0, 1, 120));
    chart.meta().offset = 0;
    for (int i = 0; i < qMax(80, 40000 / intervalMs); ++i) {
        const int timeMs = 137 + intervalMs * i;
        chart.addNote(Note(timeMs / 500, timeMs % 500, 500, 256));
    }
    if (parser.isSet("chart")) {
        QFile input(parser.value("chart"));
        if (!input.open(QIODevice::ReadOnly)) return 8;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(input.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) return 8;
        const auto root = document.object();
        const auto meta = root["meta"].toObject();
        chart.notes().clear();
        chart.bpmList().clear();
        chart.meta().offset = meta["offset"].toInt();
        QString audioName = meta["audio"].toString();
        for (const auto &v : root["time"].toArray()) {
            const auto entry = v.toObject(); const auto beat = entry["beat"].toArray();
            if (beat.size() != 3) return 8;
            chart.addBpm(BpmEntry(beat[0].toInt(), beat[1].toInt(), beat[2].toInt(), entry["bpm"].toDouble()));
        }
        for (const auto &v : root["note"].toArray()) {
            const auto note = v.toObject(); const auto beat = note["beat"].toArray();
            if (note["type"].toInt() == 1) {
                if (audioName.isEmpty()) audioName = note["sound"].toString();
                if (!chart.meta().offset) chart.meta().offset = note["offset"].toInt();
            } else if (beat.size() == 3) {
                chart.addNote(Note(beat[0].toInt(), beat[1].toInt(), beat[2].toInt(), note["x"].toInt()));
            }
        }
        musicPath = QFileInfo(input).dir().absoluteFilePath(audioName);
        if (chart.bpmList().isEmpty() || audioName.isEmpty()) return 8;
    }
    ChartController charts;
    if (!charts.loadChartFromData(temporary.filePath("probe.mc"), chart)) return 4;
    QVector<double> targetTimes;
    const auto timing = MathUtils::buildBpmTimeCache(chart.bpmList(), chart.meta().offset);
    for (const auto &note : chart.notes()) {
        const double time = MathUtils::beatToMs(note.beatNum, note.numerator, note.denominator, timing);
        if (time > parser.value("start-ms").toDouble()) targetTimes.append(time);
    }
    std::sort(targetTimes.begin(), targetTimes.end());
    QJsonArray noteTimes;
    double previous = -1e100;
    for (double time : targetTimes) {
        if (time > previous + 1e-6) noteTimes.append(time);
        previous = time;
    }
    ChartCanvas canvas;
    canvas.setChartController(&charts);
    canvas.setPlaybackController(&playback);
    canvas.setNoteSoundFile(hitPath);
    canvas.setNoteSoundEnabled(true);
    if (parser.isSet("muted")) canvas.setNoteSoundVolume(0);
    auto *sounds = canvas.findChild<NoteSoundPlayer *>();
    QJsonArray ticks, observations, calls;
    QObject::connect(sounds, &NoteSoundPlayer::hitSoundTriggered, &app, [&](qint64 steadyNs) {
        calls.append(QJsonObject{{"wall_ms", double(steadyNs) / 1e6 - startMs},
            {"predicted_ms", playback.currentTime()}, {"raw_ms", double(audio.position())}});
    });
    const int paintDelay = parser.value("paint-delay-ms").toInt();
    QObject::connect(&playback, &PlaybackController::playbackFrameTick, &app,
        [&](double visualMs, qint64 sequence) {
            const double now = steadyMs();
            ticks.append(QJsonObject{{"wall_ms", now - startMs}, {"visual_ms", visualMs},
                {"predicted_ms", playback.currentTime()}, {"raw_ms", double(audio.position())}});
            QTimer::singleShot(paintDelay, &app, [&, sequence] { playback.acknowledgeFramePainted(sequence); });
        });
    QObject::connect(&audio, &AudioPlayer::positionChanged, &app, [&](qint64 position) {
        if (startMs) observations.append(QJsonObject{{"wall_ms", steadyMs() - startMs},
            {"raw_ms", double(position)}, {"predicted_ms", playback.currentTime()}});
    });
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Loopback loopback;
    loopback.musicFrequency = 440;
    const bool capturing = !parser.isSet("no-loopback") && loopback.start();
    QTimer captureTimer;
    captureTimer.setTimerType(Qt::PreciseTimer);
    captureTimer.setInterval(2);
    QObject::connect(&captureTimer, &QTimer::timeout, &app, [&] { loopback.poll(); });
    captureTimer.start();
    bool started = false;
    QTimer readiness;
    readiness.setInterval(10);
    QObject::connect(&readiness, &QTimer::timeout, &app, [&] {
        const auto voices = canvas.findChildren<QSoundEffect *>();
        bool ready = !voices.isEmpty();
        for (const auto *voice : voices) ready = ready && voice->status() == QSoundEffect::Ready;
        if (!audio.canPlay() || !ready || started) return;
        started = true;
        readiness.stop();
        startMs = steadyMs();
        playback.seekTo(parser.value("start-ms").toDouble());
        playback.play();
        const int stall = parser.value("stall-ms").toInt();
        if (stall > 0) QTimer::singleShot(2000, &app, [stall] { Sleep(DWORD(stall)); });
        QTimer::singleShot(parser.value("duration-ms").toInt(), &app, [&] {
            playback.pause();
            QTimer::singleShot(250, &app, [&] {
                loopback.poll();
                QJsonArray musicOnsets, hitOnsets;
                for (const auto &v : loopback.music) if (v.toDouble() >= startMs) musicOnsets.append(v.toDouble() - startMs);
                for (const auto &v : loopback.hits) if (v.toDouble() >= startMs) hitOnsets.append(v.toDouble() - startMs);
                QJsonObject result{{"speed", playback.speed()}, {"qt_version", qVersion()},
                    {"chart", parser.value("chart")}, {"chart_offset", chart.meta().offset},
                    {"start_ms", parser.value("start-ms").toDouble()},
                    {"device", QMediaDevices::defaultAudioOutput().description()},
                    {"file_playback", parser.isSet("file-playback")},
                    {"streaming", parser.isSet("streaming")},
                    {"decoded_buffers", decodedBuffers},
                    {"position_granularity_ms", audio.positionGranularityMs()},
                    {"voice_restart_count", double(sounds->restartCount())},
                    {"hit_file", parser.isSet("hit-file") ? hitPath : QString("synthetic")},
                    {"synthetic_interval_ms", intervalMs},
                    {"submission_policy", "latest_due_per_wakeup"},
                    {"note_times_ms", noteTimes},
                    {"loopback_ok", capturing}, {"loopback_error", loopback.error},
                    {"paint_delay_ms", paintDelay}, {"stall_ms", stall},
                    {"ticks", ticks}, {"observations", observations}, {"play_calls", calls},
                    {"music_onsets_wall_ms", musicOnsets}, {"hit_onsets_wall_ms", hitOnsets}};
                QFile output(parser.value("output"));
                if (!output.open(QIODevice::WriteOnly)) { app.exit(5); return; }
                output.write(QJsonDocument(result).toJson());
                QTextStream(stdout) << "music=" << musicOnsets.size() << " hits=" << hitOnsets.size()
                    << " calls=" << calls.size() << " loopback=" << capturing << Qt::endl;
                app.exit(capturing || parser.isSet("no-loopback") ? 0 : 6);
            });
        });
    });
    audio.load(musicPath);
    readiness.start();
    QTimer::singleShot(15000 + parser.value("duration-ms").toInt(), &app, [&] { app.exit(7); });
    return app.exec();
}
