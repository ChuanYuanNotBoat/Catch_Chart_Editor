#include "WindowsAudioOutput.h"
#include "utils/PlaybackSpeed.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QDir>
#include <QEventLoop>
#include <QTemporaryFile>
#include <QTimer>
#include <QUrl>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr int sampleRate = 48000;
constexpr int channels = 2;
constexpr int frameBytes = channels * sizeof(float);
constexpr qint64 maxCacheBytes = 1024LL * 1024 * 1024;

struct Event
{
    HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ~Event() { if (handle) CloseHandle(handle); }
};

struct ComApartment
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComApartment() { if (SUCCEEDED(result)) CoUninitialize(); }
};

struct AudioThreadPriority
{
    DWORD taskIndex = 0;
    HANDLE handle = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
    ~AudioThreadPriority() { if (handle) AvRevertMmThreadCharacteristics(handle); }
};

double qpc100Ns()
{
    static const double ticksPer100Ns = [] {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        return frequency.QuadPart / 10000000.0;
    }();
    LARGE_INTEGER time{};
    QueryPerformanceCounter(&time);
    return time.QuadPart / ticksPer100Ns;
}
}

struct WindowsAudioOutput::Pcm
{
    QTemporaryFile file{QDir::tempPath() + QStringLiteral("/cce_pcm_XXXXXX")};
    const float *samples = nullptr;
    qint64 frames = 0;
};

std::shared_ptr<const WindowsAudioOutput::Pcm> WindowsAudioOutput::decode(
    const QString &path, const std::atomic_bool &cancel)
{
    auto pcm = std::make_shared<Pcm>();
    if (cancel.load() || !pcm->file.open())
        return {};

    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelConfig(QAudioFormat::ChannelConfigStereo);
    format.setSampleFormat(QAudioFormat::Float);
    QAudioDecoder decoder;
    decoder.setAudioFormat(format);
    decoder.setSource(QUrl::fromLocalFile(path));
    QEventLoop loop;
    bool done = false;
    bool failed = false;
    QObject::connect(&decoder, &QAudioDecoder::bufferReady, &loop, [&] {
        const QAudioBuffer buffer = decoder.read();
        if (!buffer.isValid())
            return;
        const auto actual = buffer.format();
        if (cancel.load() || actual.sampleRate() != sampleRate ||
            actual.channelCount() != channels || actual.sampleFormat() != QAudioFormat::Float ||
            buffer.byteCount() > maxCacheBytes - pcm->file.pos() ||
            pcm->file.write(buffer.constData<char>(), buffer.byteCount()) != buffer.byteCount())
        {
            failed = true;
            loop.quit();
        }
    });
    QObject::connect(&decoder, &QAudioDecoder::finished, &loop, [&] {
        done = true;
        loop.quit();
    });
    QObject::connect(&decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
                     &loop, [&](QAudioDecoder::Error error) {
        failed = error != QAudioDecoder::NoError;
        if (failed)
            loop.quit();
    });
    QTimer cancellationTimer;
    QObject::connect(&cancellationTimer, &QTimer::timeout, &loop, [&] {
        if (cancel.load())
            loop.quit();
    });
    cancellationTimer.start(25);
    decoder.start();
    if (!done && !failed && !cancel.load())
        loop.exec();
    decoder.stop();

    if (failed || !done || cancel.load() || pcm->file.pos() == 0 || !pcm->file.flush())
        return {};
    pcm->frames = pcm->file.size() / frameBytes;
    pcm->samples = reinterpret_cast<const float *>(pcm->file.map(0, pcm->file.size()));
    if (!pcm->samples)
        return {};
    pcm->file.moveToThread(nullptr);
    return pcm;
}

struct WindowsAudioOutput::Impl
{
    struct Request
    {
        double startFrame = 0;
        double endFrame = 0;
        double rate = 1;
        bool playing = false;
        quint64 generation = 0;
        quint64 deviceGeneration = 0;
    };

    struct Clock
    {
        Status status = Status::Waiting;
        double frames = 0;
        double submitted = 0;
        double qpc = 0;
    };

    explicit Impl(std::shared_ptr<const Pcm> source) : pcm(std::move(source))
    {
        if (command.handle && ready.handle)
            worker = std::thread([this] { run(); });
        else
            clock.status = Status::Failed;
    }

    ~Impl()
    {
        if (!worker.joinable())
            return;
        quitting.store(true);
        SetEvent(command.handle);
        worker.join();
    }

    bool publish(const Request &active, Clock updated)
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (active.generation != request.generation)
            return false;
        clock = updated;
        return true;
    }

    HRESULT open()
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> endpoint;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(enumerator.GetAddressOf()));
        if (SUCCEEDED(result))
            result = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, endpoint.GetAddressOf());
        if (SUCCEEDED(result))
            result = endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                        reinterpret_cast<void **>(client.GetAddressOf()));
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = channels;
        format.nSamplesPerSec = sampleRate;
        format.wBitsPerSample = sizeof(float) * 8;
        format.nBlockAlign = frameBytes;
        format.nAvgBytesPerSec = sampleRate * frameBytes;
        if (SUCCEEDED(result))
            result = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                    AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_NOPERSIST,
                0, 0, &format, nullptr);
        if (SUCCEEDED(result))
            result = client->SetEventHandle(ready.handle);
        if (SUCCEEDED(result))
            result = client->GetBufferSize(&capacity);
        if (SUCCEEDED(result))
            result = client->GetService(IID_PPV_ARGS(render.GetAddressOf()));
        if (SUCCEEDED(result))
            result = client->GetService(IID_PPV_ARGS(deviceClock.GetAddressOf()));
        if (SUCCEEDED(result))
            result = deviceClock->GetFrequency(&frequency);
        return SUCCEEDED(result) && frequency == 0 ? E_FAIL : result;
    }

    HRESULT fill(const Request &active, qint64 &submitted)
    {
        UINT32 padding = 0;
        const HRESULT paddingResult = client->GetCurrentPadding(&padding);
        if (FAILED(paddingResult))
            return paddingResult;
        const UINT32 available = capacity - padding;
        const qint64 remaining = static_cast<qint64>(std::ceil(
            (active.endFrame - active.startFrame) / active.rate)) - submitted;
        const auto count = static_cast<UINT32>(std::clamp<qint64>(remaining, 0, available));
        if (!available || (!count && !submitted))
            return S_OK;
        BYTE *bytes = nullptr;
        // Keep the stream clock moving through the end of a partial period.
        // These silent frames have no corresponding position in the source.
        const HRESULT bufferResult = render->GetBuffer(available, &bytes);
        if (FAILED(bufferResult))
            return bufferResult;
        const bool silent = !count || muted.load();
        if (!silent)
        {
            auto *out = reinterpret_cast<float *>(bytes);
            for (UINT32 frame = 0; frame < count; ++frame)
            {
                const double source = active.startFrame + (submitted + frame) * active.rate;
                const auto first = std::min(static_cast<qint64>(source), pcm->frames - 1);
                const auto next = std::min(first + 1, pcm->frames - 1);
                const float t = static_cast<float>(source - first);
                for (int channel = 0; channel < channels; ++channel)
                {
                    if (active.rate > 1.0)
                    {
                        // Average the source interval when decimating. A
                        // point sample here aliases badly at high speeds.
                        const double end = std::min(source + active.rate, active.endFrame);
                        double sum = 0;
                        for (auto index = first; index < end; ++index)
                        {
                            const double weight = std::min(end, double(index + 1)) -
                                                  std::max(source, double(index));
                            sum += pcm->samples[index * channels + channel] * weight;
                        }
                        *out++ = end > source ? float(sum / (end - source)) : 0.0f;
                        continue;
                    }
                    const float a = pcm->samples[first * channels + channel];
                    const float b = pcm->samples[next * channels + channel];
                    *out++ = a + t * (b - a);
                }
            }
            std::fill_n(out, (available - count) * channels, 0.0f);
        }
        const HRESULT releaseResult = render->ReleaseBuffer(available, silent ? AUDCLNT_BUFFERFLAGS_SILENT : 0);
        if (SUCCEEDED(releaseResult))
            submitted += count;
        return releaseResult;
    }

    void close()
    {
        deviceClock.Reset();
        render.Reset();
        client.Reset();
    }

    void run()
    {
        const ComApartment apartment;
        const AudioThreadPriority priority;
        const HANDLE events[] = {command.handle, ready.handle};
        Request active;
        quint64 deviceGeneration = 0;
        Clock observed;
        qint64 submitted = 0;
        qint64 cursorBase = 0;
        bool running = false;
        while (!quitting.load())
        {
            const DWORD wake = WaitForMultipleObjects(2, events, FALSE, running ? 1000 : INFINITE);
            if (quitting.load())
                break;
            HRESULT result = S_OK;
            if (wake == WAIT_OBJECT_0)
            {
                if (running)
                {
                    result = client->Stop();
                    if (SUCCEEDED(result))
                        result = client->Reset();
                    if (FAILED(result))
                        close();
                    running = false;
                }
                {
                    const std::lock_guard<std::mutex> lock(mutex);
                    active = request;
                }
                if (deviceGeneration != active.deviceGeneration)
                {
                    close();
                    deviceGeneration = active.deviceGeneration;
                }
                if (!active.playing)
                    continue;
                observed = {};
                submitted = 0;
                cursorBase = 0;
                if (FAILED(apartment.result))
                    result = apartment.result;
                else
                    result = client ? S_OK : open();
                if (SUCCEEDED(result))
                    result = fill(active, submitted);
                if (SUCCEEDED(result) && submitted == 0)
                {
                    publish(active, {Status::Ended});
                    continue;
                }
                if (SUCCEEDED(result))
                    result = client->Start();
                running = SUCCEEDED(result);
            }
            else if (wake == WAIT_OBJECT_0 + 1 && running)
            {
                UINT64 devicePosition = 0;
                UINT64 stamp = 0;
                result = deviceClock->GetPosition(&devicePosition, &stamp);
                if (result == S_OK)
                {
                    const double frames = cursorBase + double(devicePosition) / frequency * sampleRate;
                    observed = {devicePosition > 0 ? Status::Playing : Status::Waiting,
                                frames, double(submitted), double(stamp)};
                    if (frames >= submitted)
                    {
                        result = client->Stop();
                        if (SUCCEEDED(result))
                            result = client->Reset();
                        running = false;
                        if (SUCCEEDED(result) && active.startFrame + submitted * active.rate >= active.endFrame)
                            observed.status = Status::Ended;
                        else if (SUCCEEDED(result))
                        {
                            // The device ran out of submitted samples. Start
                            // a fresh stream clock at the last queued frame.
                            cursorBase = submitted;
                            observed = {Status::Waiting, double(cursorBase), double(submitted), 0};
                            result = fill(active, submitted);
                            if (SUCCEEDED(result))
                                result = client->Start();
                            running = SUCCEEDED(result);
                        }
                    }
                }
                if (SUCCEEDED(result) && running)
                    result = fill(active, submitted);
                if (SUCCEEDED(result))
                {
                    observed.submitted = double(submitted);
                    publish(active, observed);
                }
            }
            else if (wake == WAIT_TIMEOUT || wake == WAIT_FAILED)
                result = E_FAIL;

            if (FAILED(result))
            {
                if (running)
                    client->Stop();
                running = false;
                close();
                observed.status = Status::Failed;
                publish(active, observed);
            }
        }
        if (running)
            client->Stop();
        close();
    }

    const std::shared_ptr<const Pcm> pcm;
    mutable std::mutex mutex;
    Request request;
    Clock clock;
    double lastCurrentFrame = 0;
    Event command;
    Event ready;
    std::atomic_bool muted{false};
    std::atomic_bool quitting{false};
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    ComPtr<IAudioClock> deviceClock;
    UINT32 capacity = 0;
    UINT64 frequency = 0;
    std::thread worker;
};

WindowsAudioOutput::WindowsAudioOutput(std::shared_ptr<const Pcm> pcm)
    : d(std::make_unique<Impl>(std::move(pcm)))
{
}

WindowsAudioOutput::~WindowsAudioOutput() = default;

void WindowsAudioOutput::setPlayback(double positionMs, double endMs, double rate,
                                    bool playing, bool reopenDevice)
{
    const std::lock_guard<std::mutex> lock(d->mutex);
    d->request.startFrame = std::clamp(positionMs * sampleRate / 1000, 0.0, double(d->pcm->frames));
    d->request.endFrame = endMs >= 0
        ? std::clamp(endMs * sampleRate / 1000, d->request.startFrame, double(d->pcm->frames))
        : double(d->pcm->frames);
    d->request.rate = PlaybackSpeed::sanitize(rate);
    d->request.playing = playing;
    ++d->request.generation;
    if (reopenDevice)
        ++d->request.deviceGeneration;
    d->clock = {};
    d->lastCurrentFrame = 0;
    if (d->worker.joinable())
        SetEvent(d->command.handle);
    else
        d->clock.status = Status::Failed;
}

void WindowsAudioOutput::setMuted(bool muted)
{
    d->muted.store(muted);
}

double WindowsAudioOutput::positionMs(qint64 aheadNs) const
{
    const std::lock_guard<std::mutex> lock(d->mutex);
    const auto &clock = d->clock;
    double frames = clock.frames;
    if (clock.status == Status::Playing)
    {
        const double elapsed = qpc100Ns() - clock.qpc + double(aheadNs) / 100.0;
        frames += std::max(0.0, elapsed) * sampleRate / 10000000;
    }
    frames = std::max(d->lastCurrentFrame, std::min(frames, clock.submitted));
    if (aheadNs == 0)
        d->lastCurrentFrame = frames;
    const double media = d->request.startFrame + frames * d->request.rate;
    return std::min(media, d->request.endFrame) * 1000 / sampleRate;
}

WindowsAudioOutput::Status WindowsAudioOutput::status() const
{
    const std::lock_guard<std::mutex> lock(d->mutex);
    return d->clock.status;
}
