#include "DisplayFrameScheduler.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <dwmapi.h>
#endif

namespace
{
    constexpr double kFallbackRefreshHz = 60.0;
    constexpr double kMinimumRefreshHz = 24.0;
    constexpr double kMaximumRefreshHz = 1000.0;

    double sanitizedRate(double value, double fallback)
    {
        if (!std::isfinite(value) || value < kMinimumRefreshHz || value > kMaximumRefreshHz)
            return fallback;
        return value;
    }
}

DisplayFrameScheduler::DisplayFrameScheduler(Callback callback)
    : m_callback(std::move(callback)),
      m_thread(&DisplayFrameScheduler::run, this)
{
}

DisplayFrameScheduler::~DisplayFrameScheduler()
{
    m_active.store(false, std::memory_order_release);
    m_shutdown.store(true, std::memory_order_release);
    m_waitCondition.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

void DisplayFrameScheduler::setRates(double displayRefreshHz, double targetFrameRateHz)
{
    const double displayHz = sanitizedRate(displayRefreshHz, kFallbackRefreshHz);
    const double targetHz = std::clamp(
        std::isfinite(targetFrameRateHz) ? targetFrameRateHz : displayHz,
        1.0,
        displayHz);
    m_displayRefreshHz.store(displayHz, std::memory_order_release);
    m_targetFrameRateHz.store(targetHz, std::memory_order_release);
    m_waitCondition.notify_all();
}

void DisplayFrameScheduler::setActive(bool active)
{
    m_active.store(active, std::memory_order_release);
    m_waitCondition.notify_all();
}

std::int64_t DisplayFrameScheduler::steadyNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool DisplayFrameScheduler::waitForDisplayRefresh(double displayRefreshHz)
{
#ifdef Q_OS_WIN
    const std::int64_t beforeNs = steadyNowNs();
    const HRESULT result = DwmFlush();
    const std::int64_t elapsedNs = steadyNowNs() - beforeNs;
    if (SUCCEEDED(result) && elapsedNs >= 1000000LL)
        return true;
#endif

    const double refreshHz = sanitizedRate(displayRefreshHz, kFallbackRefreshHz);
    const auto duration = std::chrono::nanoseconds(
        static_cast<std::int64_t>(std::llround(1000000000.0 / refreshHz)));
    std::unique_lock<std::mutex> lock(m_waitMutex);
    m_waitCondition.wait_for(lock, duration, [this]()
                             {
        return m_shutdown.load(std::memory_order_acquire) ||
               !m_active.load(std::memory_order_acquire); });
    return false;
}

void DisplayFrameScheduler::run()
{
    double frameAccumulator = 0.0;
    std::int64_t lastSelectedPulseNs = 0;
    std::int64_t sequence = 0;
    bool wasActive = false;

    while (!m_shutdown.load(std::memory_order_acquire))
    {
        if (!m_active.load(std::memory_order_acquire))
        {
            std::unique_lock<std::mutex> lock(m_waitMutex);
            m_waitCondition.wait(lock, [this]()
                                 {
                return m_shutdown.load(std::memory_order_acquire) ||
                       m_active.load(std::memory_order_acquire); });
            wasActive = false;
            continue;
        }

        const double displayHz = m_displayRefreshHz.load(std::memory_order_acquire);
        const double targetHz = m_targetFrameRateHz.load(std::memory_order_acquire);
        if (!wasActive)
        {
            frameAccumulator = displayHz > targetHz ? displayHz - targetHz : 0.0;
            lastSelectedPulseNs = 0;
            wasActive = true;
        }

        waitForDisplayRefresh(displayHz);
        if (m_shutdown.load(std::memory_order_acquire) ||
            !m_active.load(std::memory_order_acquire))
        {
            continue;
        }

        // targetHz is chosen as a divisor of displayHz. The accumulator keeps
        // rate changes safe without restarting the worker.
        frameAccumulator += targetHz;
        if (frameAccumulator + 1e-6 < displayHz)
            continue;
        frameAccumulator -= displayHz;

        const std::int64_t readyNs = steadyNowNs();
        const std::int64_t intervalNs = lastSelectedPulseNs > 0
                                            ? readyNs - lastSelectedPulseNs
                                            : 0;
        lastSelectedPulseNs = readyNs;
        ++sequence;
        if (m_callback)
            m_callback(Pulse{readyNs, intervalNs, sequence});
    }
}
