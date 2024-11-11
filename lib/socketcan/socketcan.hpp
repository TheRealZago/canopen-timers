#ifndef CANOPEN_TIMERS_LIB_SOCKETCAN_HPP_
#define CANOPEN_TIMERS_LIB_SOCKETCAN_HPP_

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include <linux/can.h>

class SocketCAN {
public:
    static constexpr size_t MaxFramePayloadLen { 8 };

    using FramePayload = std::array<uint8_t, MaxFramePayloadLen>;
    using OnDataRXCallback = std::function<void(uint32_t, bool, uint8_t, const FramePayload&)>;

    explicit SocketCAN(const std::string& ifaceName, const int bitrate = 250000);
    ~SocketCAN();

    bool Open();
    bool Close();
    bool IsBusOff() const;
    bool Send(uint32_t id, bool id29Bit, uint8_t dlc, const FramePayload& data);
    bool Receive(uint32_t& id, bool& id29Bit, uint8_t& dlc, FramePayload& data);
    bool Poll(const OnDataRXCallback& rxClbkFunc);
    int BusLoad();
    bool SetBitrate(const int bitrate);

    inline std::string Name() const
    {
        return m_ifaceName;
    }

    inline int Bitrate() const
    {
        return m_bitrate;
    }

private:
    const int c_invalidSocket { -1 };
    const int c_busOffThreshold { 10 };

    struct BusStats {
        unsigned long long rxCount { 0 };
        unsigned long long rxBitsTotal { 0 };
        unsigned long long rxBitsPayload { 0 };
        std::chrono::steady_clock::time_point lastStat { std::chrono::steady_clock::now() };

        int Load(const int ifaceBitrate) const;
        void Reset();
    };

    int m_socket { c_invalidSocket };
    std::string m_ifaceName { "" };
    int m_txErrCnt { 0 };
    bool m_busOff { false };
    BusStats m_stats {};
    std::atomic_bool m_stopPolling { false };
    int m_bitrate { 0 };

    static unsigned long long FrameBitLength(const bool id29Bit, const uint8_t dlc, const size_t mtu);
    static std::string TranslateErrorFrame(const can_frame& frame);

    void UpdateStats(uint8_t dlc, size_t mtu, bool id29Bit);
};

#endif // CANOPEN_TIMERS_LIB_SOCKETCAN_HPP_
