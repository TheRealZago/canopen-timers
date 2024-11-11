#ifndef CANOPEN_TIMERS_SRC_CO_CAN_LINUX_HPP_
#define CANOPEN_TIMERS_SRC_CO_CAN_LINUX_HPP_

#include "co_if_can.h"
#include "socketcan/socketcan.hpp"

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

class co_can_linux {
public:
    static const CO_IF_CAN_DRV& CANDriver();
    static void SetCANInterface(const std::string& ifName);

private:
    struct RawCANFrame {
        uint32_t canId {};
        bool isExtCanId {};
        uint8_t dlc {};
        SocketCAN::FramePayload data {};
        const std::chrono::steady_clock::time_point timestamp { std::chrono::steady_clock::now() };

        RawCANFrame() = default;
        RawCANFrame(uint32_t _canId, bool _is29Bit, uint8_t _dlc, const SocketCAN::FramePayload& _data)
            : canId(_canId)
            , isExtCanId(_is29Bit)
            , dlc(_dlc)
            , data(_data) {};

        inline operator bool() const
        {
            if (canId == 0 && dlc == 0)
                return false;
            return true;
        }
    };

    static constexpr std::chrono::microseconds PollingRate { 500 };
    static constexpr size_t ReasonableFrameCount { 100 };

    static const CO_IF_CAN_DRV s_coCanDrv;

    static std::string s_ifName;
    static std::unique_ptr<SocketCAN> s_canIf;
    static std::unique_ptr<std::thread> s_rxPolling;
    static std::mutex s_rxMutex;
    static std::list<RawCANFrame> s_rxQueue;

    static void Init();
    static void Enable(uint32_t baudRate);
    static int16_t Send(CO_IF_FRM* frame);
    static int16_t Read(CO_IF_FRM* frame);
    static void Reset();
    static void Close();

    static void StartPolling();
    static void PushFrame(uint32_t canId, bool is29Bit, uint8_t dlc, const SocketCAN::FramePayload& data);
    static RawCANFrame PopFrame();
    static void ResetQueue();

    // Make it purely static
    co_can_linux() = delete;
    ~co_can_linux() = delete;
};

#endif // CANOPEN_TIMERS_SRC_CO_CAN_LINUX_HPP_
