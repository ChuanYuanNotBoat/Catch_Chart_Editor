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
    constexpr double kPhaseCorrectionGain = 0.02;
    constexpr std::int64_t kMaximumPhaseCorrectionNs = 50000;

    double sanitizedRate(double value, double fallback)
    {
        if (!std::isfinite(value) || value < kMinimumRefreshHz || value > kMaximumRefreshHz)
            return fallback;
        return value;
    }

#ifdef Q_OS_WIN
    class PlaybackThreadPriority final
    {
    public:
        PlaybackThreadPriority()
        {
            m_originalPriority = GetThreadPriority(GetCurrentThread());
            if (m_originalPriority != THREAD_PRIORITY_ERROR_RETURN)
                m_raised = SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        }

        ~PlaybackThreadPriority()
        {
            if (m_raised)
                SetThreadPriority(GetCurrentThread(), m_originalPriority);
        }

    private:
        int m_originalPriority = THREAD_PRIORITY_NORMAL;
        bool m_raised = false;
    };
#endif
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
#ifdef Q_OS_WIN
    PlaybackThreadPriority playbackThreadPriority;
#endif
    double frameAccumulator = 0.0;
    double previousDisplayHz = 0.0;
    double previousTargetHz = 0.0;
    std::int64_t lastDisplayPulseNs = 0;
    std::int64_t lastSelectedPulseNs = 0;
    std::int64_t idealSelectedPulseNs = 0;
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
            previousDisplayHz = displayHz;
            previousTargetHz = targetHz;
            lastDisplayPulseNs = 0;
            lastSelectedPulseNs = 0;
            idealSelectedPulseNs = 0;
            wasActive = true;
        }

        waitForDisplayRefresh(displayHz);
        if (m_shutdown.load(std::memory_order_acquire) ||
            !m_active.load(std::memory_order_acquire))
        {
            continue;
        }

        const std::int64_t readyNs = steadyNowNs();
        const double displayPeriodNs = 1000000000.0 / displayHz;
        const double targetPeriodNs = 1000000000.0 / targetHz;
        if (std::abs(displayHz - previousDisplayHz) > 0.01 ||
            std::abs(targetHz - previousTargetHz) > 0.01)
        {
            frameAccumulator = displayHz > targetHz ? displayHz - targetHz : 0.0;
            lastDisplayPulseNs = 0;
            lastSelectedPulseNs = 0;
            idealSelectedPulseNs = 0;
            previousDisplayHz = displayHz;
            previousTargetHz = targetHz;
        }

        // A worker can occasionally wake after more than one physical refresh.
        // Infer the elapsed refresh count so the visual timeline advances over
        // genuinely missed presentations instead of slowing down temporarily.
        std::int64_t displaySteps = 1;
        if (lastDisplayPulseNs > 0)
        {
            const double observedSteps =
                static_cast<double>(readyNs - lastDisplayPulseNs) / displayPeriodNs;
            const std::int64_t roundedSteps =
                static_cast<std::int64_t>(std::llround(observedSteps));
            displaySteps = roundedSteps > 1 ? roundedSteps : 1;
        }
        lastDisplayPulseNs = readyNs;

        // targetHz is chosen as a divisor of displayHz. Coalesce any target
        // frames missed while the worker was descheduled into the latest one.
        frameAccumulator += targetHz * static_cast<double>(displaySteps);
        if (frameAccumulator + 1e-6 < displayHz)
            continue;
        std::int64_t selectedSteps = 0;
        while (frameAccumulator + 1e-6 >= displayHz)
        {
            frameAccumulator -= displayHz;
            ++selectedSteps;
        }

        const std::int64_t intervalNs = lastSelectedPulseNs > 0
                                            ? readyNs - lastSelectedPulseNs
                                            : 0;
        lastSelectedPulseNs = readyNs;

        if (idealSelectedPulseNs == 0)
        {
            idealSelectedPulseNs = readyNs;
        }
        else
        {
            idealSelectedPulseNs += static_cast<std::int64_t>(
                std::llround(targetPeriodNs * static_cast<double>(selectedSteps)));
            const std::int64_t phaseErrorNs = readyNs - idealSelectedPulseNs;
            if (std::abs(phaseErrorNs) > static_cast<std::int64_t>(targetPeriodNs * 2.0))
            {
                idealSelectedPulseNs = readyNs;
            }
            else
            {
                const std::int64_t correctionNs = std::clamp<std::int64_t>(
                    static_cast<std::int64_t>(std::llround(
                        static_cast<double>(phaseErrorNs) * kPhaseCorrectionGain)),
                    -kMaximumPhaseCorrectionNs,
                    kMaximumPhaseCorrectionNs);
                idealSelectedPulseNs += correctionNs;
            }
        }

        // DwmFlush returns after the current composition refresh. Work posted
        // now is presented on the next physical refresh, so sample the visual
        // clock at that phase rather than at the jittery CPU wake-up time.
        const std::int64_t presentationNs =
            idealSelectedPulseNs + static_cast<std::int64_t>(std::llround(displayPeriodNs));
        if (m_callback)
            m_callback(Pulse{readyNs, intervalNs, presentationNs, selectedSteps - 1});
    }
}
