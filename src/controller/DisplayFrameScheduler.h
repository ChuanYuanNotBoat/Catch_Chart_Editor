#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

// Produces one callback per selected physical display refresh. On Windows the
// worker waits on DWM composition instead of a UI-thread timer, so a busy Qt
// event loop cannot move the pacing clock itself. Consumers must coalesce work
// if they have not finished the previous frame.
class DisplayFrameScheduler final
{
public:
    struct Pulse
    {
        std::int64_t readySteadyNs = 0;
        std::int64_t intervalNs = 0;
    };

    using Callback = std::function<void(const Pulse &)>;

    explicit DisplayFrameScheduler(Callback callback);
    ~DisplayFrameScheduler();

    DisplayFrameScheduler(const DisplayFrameScheduler &) = delete;
    DisplayFrameScheduler &operator=(const DisplayFrameScheduler &) = delete;

    void setRates(double displayRefreshHz, double targetFrameRateHz);
    void setActive(bool active);

    static std::int64_t steadyNowNs();

private:
    void run();
    bool waitForDisplayRefresh(double displayRefreshHz);

    Callback m_callback;
    std::atomic<bool> m_shutdown{false};
    std::atomic<bool> m_active{false};
    std::atomic<double> m_displayRefreshHz{60.0};
    std::atomic<double> m_targetFrameRateHz{60.0};
    std::mutex m_waitMutex;
    std::condition_variable m_waitCondition;
    std::thread m_thread;
};
