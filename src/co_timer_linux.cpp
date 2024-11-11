#include "co_timer_linux.hpp"
#include "co_core.h"
#include "co_tmr.h"

#include <algorithm>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>

#define CO_TIMER_OS_SOURCE (CLOCK_MONOTONIC)

static const std::string LOG_MARKER { "[HAL::TMR] " };
static const std::string ERR_MARKER { "E: " };
static const std::string DBG_MARKER { "D: " };

// Quick constants for nanoseconds handling
static constexpr co_timer_linux::TimeUnit T1000ms { 1'000'000'000 };
static constexpr co_timer_linux::TimeUnit T1ms { T1000ms / 1000 };

// Overriding weak linking for internal stack timer locks
extern "C" {
void COTmrLock()
{
    co_timer_linux::Lock();
}

void COTmrUnlock()
{
    co_timer_linux::Unlock();
}
};

const CO_IF_TIMER_DRV& co_timer_linux::TimerDriver()
{
    return s_coTmrDrv;
}

void co_timer_linux::LinkTimer(CO_TMR* tmr)
{
    s_tmr = tmr;
}

void co_timer_linux::Lock()
{
    s_lock.lock();
}

void co_timer_linux::Unlock()
{
    s_lock.unlock();
}

const CO_IF_TIMER_DRV co_timer_linux::s_coTmrDrv {
    co_timer_linux::Init,
    co_timer_linux::Reload,
    co_timer_linux::Delay,
    co_timer_linux::Stop,
    co_timer_linux::Start,
    co_timer_linux::Update,
};

co_timer_linux::TimeUnit co_timer_linux::s_tickRateNanoSec { T1ms }; // 1ms by default
CO_TMR* co_timer_linux::s_tmr { nullptr };
std::mutex co_timer_linux::s_lock {};
timer_t co_timer_linux::s_timerId {};
co_timer_linux::TimeUnit co_timer_linux::s_tempSec { 0 };
co_timer_linux::TimeUnit co_timer_linux::s_tempNanosec { 0 };

void co_timer_linux::Init(uint32_t freq)
{
    struct timespec timeRes { };
    const auto rc = clock_getres(CO_TIMER_OS_SOURCE, &timeRes);
    const auto tempErrno = errno;
    if (rc == 0) {
        std::cout << LOG_MARKER << "Selected timer resolution: " << timeRes.tv_sec << "s " << timeRes.tv_nsec << "ns"
                  << std::endl;
    } else {
        std::cerr << ERR_MARKER << LOG_MARKER << "clock_getres failed with errno " << tempErrno << std::endl;
    }

    s_tickRateNanoSec = std::max(1UL, T1000ms / freq); // Frequency in hertz to period in ns
    std::cout << LOG_MARKER << "Expected tick frequency " << freq << " Hz, effective tick precision "
              << s_tickRateNanoSec << " ns" << std::endl;

    CreateOSTimer();
}

void co_timer_linux::Reload(uint32_t reload)
{
    s_tempNanosec = (reload * s_tickRateNanoSec) % T1000ms;
    s_tempSec = (reload * s_tickRateNanoSec) / T1000ms;
    if (s_timerId)
        ArmOSTimer(s_tempSec, s_tempNanosec);
}

uint32_t co_timer_linux::Delay()
{
    const auto remTime = RemainingOSTimer();
    auto ticks = (remTime.it_value.tv_sec * T1000ms) / s_tickRateNanoSec;
    ticks += (remTime.it_value.tv_nsec) / s_tickRateNanoSec;
    return static_cast<uint32_t>(ticks);
}

void co_timer_linux::Stop()
{
    if (!s_timerId)
        return;
    DisarmOSTimer();
}

void co_timer_linux::Start()
{
    ArmOSTimer(s_tempSec, s_tempNanosec);
}

uint8_t co_timer_linux::Update()
{
    return 1;
}

int co_timer_linux::CreateOSTimer()
{
    struct sigevent timerTrigger { };
    std::memset(&timerTrigger, 0, sizeof(timerTrigger));
    timerTrigger.sigev_notify = SIGEV_THREAD;
    timerTrigger.sigev_notify_function = &co_timer_linux::ISROSTimer;
    timerTrigger.sigev_notify_attributes = nullptr;

    // Allocate timer object and make it raise SIGALRM on trigger
    auto rc = timer_create(CO_TIMER_OS_SOURCE, &timerTrigger, &s_timerId);
    auto tempErrno = errno;
    if (rc != 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << "timer_create failed with errno " << tempErrno << std::endl;
    }

    return rc;
}

int co_timer_linux::RemoveOSTimer()
{
    struct sigaction timerAction { };
    std::memset(&timerAction, 0, sizeof(timerAction));
    timerAction.sa_handler = SIG_DFL;
    sigemptyset(&timerAction.sa_mask);

    // Unbind SIGALRM handler function
    auto rc = sigaction(SIGALRM, &timerAction, nullptr);
    auto tempErrno = errno;
    if (rc != 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << "sigaction failed with errno " << tempErrno << std::endl;
    }

    // Disarm and drop timer
    rc = timer_delete(&s_timerId);
    tempErrno = errno;
    if (rc != 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << "timer_delete failed with errno " << tempErrno << std::endl;
    }

    return rc;
}

int co_timer_linux::ArmOSTimer(TimeUnit periodSec, TimeUnit periodNanosec)
{
    // A relatively verbose way to do a few things:
    //   * cast using decltype() so i don't need to care about underlying type aliasing
    //   * clamp to valid values for both seconds and nanoseconds
    //   * configure timer as one-shot event
    struct itimerspec timerPeriod { };
    std::memset(&timerPeriod, 0, sizeof(timerPeriod));
    timerPeriod.it_value.tv_sec = static_cast<decltype(timerPeriod.it_value.tv_sec)>(std::max(periodSec, 0UL));
    timerPeriod.it_value.tv_nsec
        = static_cast<decltype(timerPeriod.it_value.tv_nsec)>(std::clamp(periodNanosec, 0UL, T1000ms - 1));

    auto rc = timer_settime(s_timerId, 0, &timerPeriod, nullptr);
    auto tempErrno = errno;
    if (rc != 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << "timer_settime failed with errno " << tempErrno << std::endl;
    }
    return rc;
}

int co_timer_linux::DisarmOSTimer()
{
    return ArmOSTimer(0, 0);
}

struct itimerspec co_timer_linux::RemainingOSTimer()
{
    struct itimerspec timerRemaining { };
    std::memset(&timerRemaining, 0, sizeof(timerRemaining));

    auto rc = timer_gettime(s_timerId, &timerRemaining);
    auto tempErrno = errno;
    if (rc != 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << "timer_gettime failed with errno " << tempErrno << std::endl;
    }

    return timerRemaining;
}

void co_timer_linux::ISROSTimer([[maybe_unused]] __sigval_t signum)
{
    COTmrService(s_tmr);
}
