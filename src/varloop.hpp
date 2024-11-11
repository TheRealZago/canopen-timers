#ifndef CANOPEN_TIMERS_SRC_VARLOOP_HPP_
#define CANOPEN_TIMERS_SRC_VARLOOP_HPP_

#include "mystack.hpp"

#include <chrono>
#include <cstdint>

class varloop {
public:
    explicit varloop(mystack& coStack);

    void Tick();

private:
    static constexpr std::chrono::milliseconds TickRate { 500 };

    mystack& m_coStack;
    std::chrono::steady_clock::time_point m_lastUpdate { std::chrono::steady_clock::now() - TickRate };
    uint32_t m_dataPoint1 { 0 };
    uint32_t m_dataPoint2 { UINT32_MAX };
    uint32_t m_dataPoint3 { 1 };
};

#endif // CANOPEN_TIMERS_SRC_VARLOOP_HPP_
