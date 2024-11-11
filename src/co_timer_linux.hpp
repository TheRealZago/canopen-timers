#ifndef CANOPEN_TIMERS_SRC_CO_TIMER_LINUX_HPP_
#define CANOPEN_TIMERS_SRC_CO_TIMER_LINUX_HPP_

#include "co_if_timer.h"
#include "co_tmr.h"

#include <csignal>
#include <ctime>
#include <mutex>

class co_timer_linux {
public:
    using TimeUnit = uint64_t;

    static const CO_IF_TIMER_DRV& TimerDriver();
    static void LinkTimer(CO_TMR* tmr);

    static void Lock();
    static void Unlock();

private:
    static const CO_IF_TIMER_DRV s_coTmrDrv;

    static TimeUnit s_tickRateNanoSec;
    static CO_TMR* s_tmr;
    static std::mutex s_lock;
    static timer_t s_timerId;

    static TimeUnit s_tempSec;
    static TimeUnit s_tempNanosec;

    static void Init(uint32_t freq);
    static void Reload(uint32_t reload);
    static uint32_t Delay();
    static void Stop();
    static void Start();
    static uint8_t Update();

    static int CreateOSTimer();
    static int RemoveOSTimer();
    static int ArmOSTimer(TimeUnit periodSec, TimeUnit periodNanosec);
    static int DisarmOSTimer();
    static struct itimerspec RemainingOSTimer();
    static void ISROSTimer(__sigval_t signum);

    // Make it purely static
    co_timer_linux() = delete;
    ~co_timer_linux() = delete;
};

#endif // CANOPEN_TIMERS_SRC_CO_TIMER_LINUX_HPP_
