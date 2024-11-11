#include "co_can_linux.hpp"
#include "utils.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>

static const std::string LOG_MARKER { "[HAL::CAN] " };
static const std::string ERR_MARKER { "E: " };
static const std::string DBG_MARKER { "D: " };

const CO_IF_CAN_DRV& co_can_linux::CANDriver()
{
    return s_coCanDrv;
}

void co_can_linux::SetCANInterface(const std::string& ifName)
{
    // Allow writing this internal var only if SocketCAN hasn't been created yet
    if (!s_canIf)
        s_ifName = ifName;
}

const CO_IF_CAN_DRV co_can_linux::s_coCanDrv {
    co_can_linux::Init,
    co_can_linux::Enable,
    co_can_linux::Read,
    co_can_linux::Send,
    co_can_linux::Reset,
    co_can_linux::Close,
};

std::string co_can_linux::s_ifName {};
std::unique_ptr<SocketCAN> co_can_linux::s_canIf {};
std::unique_ptr<std::thread> co_can_linux::s_rxPolling {};
std::mutex co_can_linux::s_rxMutex {};
std::list<co_can_linux::RawCANFrame> co_can_linux::s_rxQueue {};

void co_can_linux::Init()
{
    if (!s_canIf)
        s_canIf
            = std::make_unique<SocketCAN>(s_ifName);

    if (!s_canIf)
        std::cerr << ERR_MARKER << LOG_MARKER << "Failed to initialize CAN port" << std::endl;
    else
        std::cout << LOG_MARKER << "Initialized on " << s_canIf->Name() << std::endl;
}

void co_can_linux::Enable(uint32_t baudRate)
{
    if (!s_canIf)
        return;

    s_canIf->Close();
    s_canIf->SetBitrate(baudRate);

    if (baudRate != s_canIf->Bitrate())
        std::cerr << ERR_MARKER << LOG_MARKER << "Failed to change port bitrate" << std::endl;
    else
        std::cout << LOG_MARKER << "Now running at " << baudRate << " bps" << std::endl;

    std::cout << LOG_MARKER << "Starting..." << std::endl;
    s_canIf->Open();
    StartPolling();
}

int16_t co_can_linux::Send(CO_IF_FRM* frame)
{
    if (!s_canIf)
        return -1;

    if (s_canIf->IsBusOff())
        return -1;

    SocketCAN::FramePayload data {};
    std::copy(std::begin(frame->Data), std::end(frame->Data), data.begin());
    if (!s_canIf->Send(frame->Identifier, false, frame->DLC, data)) {
        return -1;
    }
#ifndef NDEBUG
    std::cout << DBG_MARKER << LOG_MARKER << "> TX " << utils::ToHex(frame->Identifier, true) << " "
              << utils::DumpBuffer(frame->Data, frame->DLC) << std::endl;
#endif
    return 0;
}

int16_t co_can_linux::Read(CO_IF_FRM* frame)
{
    if (!s_canIf)
        return -1;

    auto sktFrm = PopFrame();
    if (!sktFrm)
        return 0;

    frame->Identifier = sktFrm.canId;
    frame->DLC = sktFrm.dlc;
    std::memcpy(frame->Data, sktFrm.data.data(), std::min(sizeof(frame->Data), sktFrm.data.size()));
#ifndef NDEBUG
    std::cout << DBG_MARKER << LOG_MARKER << "< RX " << utils::ToHex(frame->Identifier, true) << " "
              << utils::DumpBuffer(frame->Data, frame->DLC) << std::endl;
#endif
    return frame->DLC;
}

void co_can_linux::Reset()
{
    if (!s_canIf)
        return;

    std::cout << LOG_MARKER << "Resetting..." << std::endl;
    // s_canIf->Close();
    // std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // s_canIf->Open();
}

void co_can_linux::Close()
{
    if (!s_canIf)
        return;

    std::cout << LOG_MARKER << "Closing..." << std::endl;
    s_canIf->Close();
    s_canIf.reset();
}

void co_can_linux::StartPolling()
{
    s_canIf->Close();
    if (s_rxPolling && s_rxPolling->joinable()) {
        s_rxPolling->join();
    }
    ResetQueue();
    s_canIf->Open();
    s_rxPolling = std::make_unique<std::thread>(&SocketCAN::Poll, s_canIf.get(), &co_can_linux::PushFrame);
}

void co_can_linux::PushFrame(uint32_t canId, bool is29Bit, uint8_t dlc, const SocketCAN::FramePayload& data)
{
    std::scoped_lock rxLock(s_rxMutex);
    s_rxQueue.emplace_back(canId, is29Bit, dlc, data);

    if (s_rxQueue.size() > ReasonableFrameCount) {
        std::cout << "W: " << LOG_MARKER << s_canIf->Name() << ": rx queue has collected >" << ReasonableFrameCount
                  << " frames! Expect dispatch delays!" << std::endl;
    }
}

co_can_linux::RawCANFrame co_can_linux::PopFrame()
{
    if (!s_rxMutex.try_lock())
        return {};

    if (s_rxQueue.empty()) {
        s_rxMutex.unlock();
        return {};
    }

    auto output = s_rxQueue.front();
    s_rxQueue.pop_front();
    s_rxMutex.unlock();
    return output;
}

void co_can_linux::ResetQueue()
{
    std::scoped_lock rxLock(s_rxMutex);
    s_rxQueue.clear();
}
