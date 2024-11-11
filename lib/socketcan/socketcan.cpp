#include "socketcan.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

#include <dirent.h>
#include <unistd.h>

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <netlink/netlink.h>
#include <netlink/route/link.h>
#include <netlink/route/link/can.h>
#include <netlink/socket.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

const std::string LOG_MARKER { "[SocketCAN] " };
const std::string ERR_MARKER { "E: " };
const std::string DBG_MARKER { "D: " };

static const std::map<uint32_t, std::string> ErrorClassBits {
    { CAN_ERR_TX_TIMEOUT, "TX timeout" },
    { CAN_ERR_LOSTARB, "" }, // compose message after the fact
    { CAN_ERR_CRTL, "" }, // compose message after the fact
    { CAN_ERR_PROT, "" }, // compose message after the fact
    { CAN_ERR_TRX, "" }, // compose message after the fact
    { CAN_ERR_ACK, "no ACK on TX" },
    { CAN_ERR_BUSOFF, "bus off" },
    { CAN_ERR_BUSERROR, "bus error" },
    { CAN_ERR_RESTARTED, "controller restarted" },
};

static const std::map<uint8_t, std::string> ControllerErrorStatus {
    { CAN_ERR_CRTL_RX_OVERFLOW, "RX buffer overflow" },
    { CAN_ERR_CRTL_TX_OVERFLOW, "TX buffer overflow" },
    { CAN_ERR_CRTL_RX_WARNING, "RX warning" },
    { CAN_ERR_CRTL_TX_WARNING, "TX warning" },
    { CAN_ERR_CRTL_RX_PASSIVE, "RX passive error" },
    { CAN_ERR_CRTL_TX_PASSIVE, "TX passive error" },
    { CAN_ERR_CRTL_ACTIVE, "recovered to active error" },
};

static const std::map<uint8_t, std::string> ProtocolErrorType {
    { CAN_ERR_PROT_BIT, "single bit error" },
    { CAN_ERR_PROT_FORM, "frame format error" },
    { CAN_ERR_PROT_STUFF, "bit stuffing error" },
    { CAN_ERR_PROT_BIT0, "unable to send dominant bit" },
    { CAN_ERR_PROT_BIT1, "unable to send recessive bit" },
    { CAN_ERR_PROT_OVERLOAD, "bus overload" },
    { CAN_ERR_PROT_ACTIVE, "active error announcement" },
    { CAN_ERR_PROT_TX, "TX failure" },
};

static const std::map<uint8_t, std::string> ProtocolErrorLocation {
    { CAN_ERR_PROT_LOC_SOF, "start of frame" },
    { CAN_ERR_PROT_LOC_ID28_21, "ID [28-21]" },
    { CAN_ERR_PROT_LOC_ID20_18, "ID [20-18]" },
    { CAN_ERR_PROT_LOC_SRTR, "SRTR" },
    { CAN_ERR_PROT_LOC_IDE, "ID extension" },
    { CAN_ERR_PROT_LOC_ID17_13, "ID [17-13]" },
    { CAN_ERR_PROT_LOC_ID12_05, "ID [12-5]" },
    { CAN_ERR_PROT_LOC_ID04_00, "ID [4-0]" },
    { CAN_ERR_PROT_LOC_RTR, "RTR" },
    { CAN_ERR_PROT_LOC_RES1, "reserved 1" },
    { CAN_ERR_PROT_LOC_RES0, "reserved 0" },
    { CAN_ERR_PROT_LOC_DLC, "DLC" },
    { CAN_ERR_PROT_LOC_DATA, "payload" },
    { CAN_ERR_PROT_LOC_CRC_SEQ, "CRC" },
    { CAN_ERR_PROT_LOC_CRC_DEL, "CRC delimiter" },
    { CAN_ERR_PROT_LOC_ACK, "ACK" },
    { CAN_ERR_PROT_LOC_ACK_DEL, "ACK delimiter" },
    { CAN_ERR_PROT_LOC_EOF, "end of frame" },
    { CAN_ERR_PROT_LOC_INTERM, "intermission" },
};

static const std::map<uint8_t, std::string> TransceiverError {
    { CAN_ERR_TRX_CANH_NO_WIRE, "no wire on CAN_H" },
    { CAN_ERR_TRX_CANH_SHORT_TO_BAT, "CAN_H shorted to Vbatt" },
    { CAN_ERR_TRX_CANH_SHORT_TO_VCC, "CAN_H shorted to Vcc" },
    { CAN_ERR_TRX_CANH_SHORT_TO_GND, "CAN_H shorted to ground" },
    { CAN_ERR_TRX_CANL_NO_WIRE, "no wire on CAN_L" },
    { CAN_ERR_TRX_CANL_SHORT_TO_BAT, "CAN_L shorted to Vbatt" },
    { CAN_ERR_TRX_CANL_SHORT_TO_VCC, "CAN_L shorted to Vcc" },
    { CAN_ERR_TRX_CANL_SHORT_TO_GND, "CAN_L shorted to ground" },
    { CAN_ERR_TRX_CANL_SHORT_TO_CANH, "CAN_L shorted to CAN_H" },
};

static bool Netlink_Connect(nl_sock*& sock, nl_cache*& cache)
{
    sock = nl_socket_alloc();
    if (!sock) {
        std::cerr << ERR_MARKER << LOG_MARKER << "NL: failed to allocate socket!" << std::endl;
        return false;
    }

    // Connect to the routing netlink protocol
    auto rc = nl_connect(sock, NETLINK_ROUTE);
    if (rc < 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << "NL: failed to connect socket (code " << rc << ")!" << std::endl;
        return false;
    }

    // Get the link cache
    rc = rtnl_link_alloc_cache(sock, AF_UNSPEC, &cache);
    if (rc < 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << "NL: failed to allocate cache (code " << rc << ")!" << std::endl;
        return false;
    }

    return sock && cache;
}

static void Netlink_Dispose(nl_sock*& sock, nl_cache*& cache)
{
    if (cache)
        nl_cache_free(cache);

    if (sock)
        nl_socket_free(sock);
}

static bool Netlink_GetInterface(nl_sock*& sock, nl_cache*& cache, rtnl_link*& link, const std::string& ifaceName)
{
    if (!sock || !cache)
        return false;

    // Find the interface index by name
    auto ifindex = rtnl_link_name2i(cache, ifaceName.c_str());
    if (ifindex == 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << "NL: " << std::quoted(ifaceName) << " not found" << std::endl;
        return false;
    }

    // Get the existing link object for the CAN interface
    link = rtnl_link_get(cache, ifindex);
    return !!link;
}

static void Netlink_DisposeInterface(rtnl_link*& link)
{
    if (link)
        rtnl_link_put(link);
}

static bool Netlink_BringUp(nl_sock*& sock, rtnl_link*& link)
{
    if (!sock || !link)
        return false;

    if ((rtnl_link_get_flags(link) & IFF_UP) != 0)
        return true;

    auto change = rtnl_link_alloc();
    if (!change)
        return false;

    const auto ifidx = rtnl_link_get_ifindex(link);
    const auto ifname = rtnl_link_get_name(link);
    rtnl_link_set_ifindex(change, ifidx);
    rtnl_link_set_flags(change, IFF_UP);

    // Apply the changes
    const auto rc = rtnl_link_change(sock, link, change, 0);
    if (rc < 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << ifname << ": failed to bring up (code " << rc << ")!" << std::endl;
    } else {
        std::cout << LOG_MARKER << ifname << ": interface is UP!" << std::endl;
    }

    rtnl_link_put(change);
    return rc == 0;
}

static bool Netlink_BringDown(nl_sock*& sock, rtnl_link*& link)
{
    if (!sock || !link)
        return false;

    if ((rtnl_link_get_flags(link) & IFF_UP) == 0)
        return true;

    auto change = rtnl_link_alloc();
    if (!change)
        return false;

    const auto ifidx = rtnl_link_get_ifindex(link);
    const auto ifname = rtnl_link_get_name(link);
    rtnl_link_set_ifindex(change, ifidx);
    rtnl_link_unset_flags(change, IFF_UP);

    // Apply the changes
    const auto rc = rtnl_link_change(sock, link, change, 0);
    if (rc < 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << ifname << ": failed to bring down (code " << rc << ")!" << std::endl;
    } else {
        std::cout << LOG_MARKER << ifname << ": interface is DOWN!" << std::endl;
    }

    rtnl_link_put(change);
    return rc == 0;
}

static bool Netlink_CANSetBitrate(nl_sock*& sock, rtnl_link*& link, const int bitrate)
{
    if (!sock || !link)
        return false;

    if (!rtnl_link_is_can(link)) {
        std::cerr << ERR_MARKER << LOG_MARKER << "NL: can't set CAN bitrate on a non-CAN interface!" << std::endl;
        return false;
    }

    uint32_t currBitrate = 0;
    if (rtnl_link_can_get_bitrate(link, &currBitrate) == 0 && currBitrate == bitrate)
        return true;

    auto change = rtnl_link_alloc();
    if (!change)
        return false;

    const auto ifidx = rtnl_link_get_ifindex(link);
    const auto ifname = rtnl_link_get_name(link);
    rtnl_link_set_ifindex(change, ifidx);
    rtnl_link_set_type(change, "can");
    rtnl_link_can_set_bitrate(change, bitrate);

    // Apply the changes
    const auto rc = rtnl_link_change(sock, link, change, 0);
    if (rc < 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << ifname << ": failed to set bitrate (code " << rc << ")!" << std::endl;
    } else {
        std::cout << LOG_MARKER << ifname << ": bitrate set to " << bitrate << std::endl;
    }

    rtnl_link_put(change);
    return rc == 0;
}

static bool Netlink_SetTXQueueLen(nl_sock*& sock, rtnl_link*& link, const size_t qlen)
{
    if (!sock || !link)
        return false;

    if (rtnl_link_get_txqlen(link) == qlen)
        return true;

    auto change = rtnl_link_alloc();
    if (!change)
        return false;

    const auto ifidx = rtnl_link_get_ifindex(link);
    const auto ifname = rtnl_link_get_name(link);
    rtnl_link_set_ifindex(change, ifidx);
    rtnl_link_set_txqlen(change, qlen);

    // Apply the changes
    const auto rc = rtnl_link_change(sock, link, change, 0);
    if (rc < 0) {
        std::cerr << ERR_MARKER << LOG_MARKER << ifname << ": failed to set TX queue length (code " << rc << ")!"
                  << std::endl;
    }

    rtnl_link_put(change);
    return rc == 0;
}

SocketCAN::SocketCAN(const std::string& ifaceName, const int bitrate)
    : m_ifaceName(ifaceName)
    , m_bitrate(bitrate)
{
    // SetBitrate(m_bitrate);
}

SocketCAN::~SocketCAN()
{
    Close();

    nl_sock* sock = nullptr;
    nl_cache* cache = nullptr;
    rtnl_link* link = nullptr;

    Netlink_Connect(sock, cache);
    const auto ok = Netlink_GetInterface(sock, cache, link, m_ifaceName);
    if (ok) {
        Netlink_BringDown(sock, link);
    }
    Netlink_DisposeInterface(link);
    Netlink_Dispose(sock, cache);
}

bool SocketCAN::Open()
{
    if (m_socket != c_invalidSocket) {
        return true;
    }

    nl_sock* sock = nullptr;
    nl_cache* cache = nullptr;
    rtnl_link* link = nullptr;

    Netlink_Connect(sock, cache);
    const auto ok = Netlink_GetInterface(sock, cache, link, m_ifaceName);
    if (ok) {
        Netlink_CANSetBitrate(sock, link, m_bitrate);
        Netlink_SetTXQueueLen(sock, link, 1000);
        Netlink_BringUp(sock, link);
    }
    Netlink_DisposeInterface(link);
    Netlink_Dispose(sock, cache);

    m_socket = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (m_socket < 0) {
        auto tempErrCode = errno;
        std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName
                  << ": failed to allocate socket file descriptor, error code " << tempErrCode << std::endl;
        m_socket = c_invalidSocket;
        return false;
    }

    std::cout << LOG_MARKER << "Opening `" << m_ifaceName << "`..." << std::endl;
    struct ifreq ifr;
    std::strcpy(ifr.ifr_name, m_ifaceName.c_str());
    auto ret = ioctl(m_socket, SIOCGIFINDEX, &ifr);
    if (ret < 0) {
        auto tempErrCode = errno;
        std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName << ": failed to find interface, error code " << tempErrCode
                  << std::endl;
    } else {
        struct sockaddr_can addr;
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        ret = bind(m_socket, (struct sockaddr*)&addr, sizeof(addr));
        if (0U != ret) {
            auto tempErrCode = errno;
            std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName << ": failed to bind, error code " << tempErrCode
                      << std::endl;
            m_socket = c_invalidSocket;
        } else {
            const can_err_mask_t errMask = CAN_ERR_TX_TIMEOUT | CAN_ERR_LOSTARB | CAN_ERR_CRTL | CAN_ERR_PROT
                | CAN_ERR_TRX | CAN_ERR_ACK | CAN_ERR_BUSOFF | CAN_ERR_BUSERROR | CAN_ERR_RESTARTED;
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 500;

            auto rc = setsockopt(m_socket, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &errMask, sizeof(errMask));
            if (rc != 0) {
                auto tempErrCode = errno;
                std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName << ": failed to set error mask filter, error code "
                          << tempErrCode << std::endl;
            }
            rc = setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
            if (rc != 0) {
                auto tempErrCode = errno;
                std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName << ": failed to set socket TX timeout, error code "
                          << tempErrCode << std::endl;
            }
            std::cout << LOG_MARKER << m_ifaceName << ": ready!" << std::endl;
            return true;
        }
    }

    return false;
}

bool SocketCAN::Close()
{
    if (c_invalidSocket != m_socket) {
        m_stopPolling.store(true);
        shutdown(m_socket, SHUT_RDWR);
        close(m_socket);
        m_socket = c_invalidSocket;
    }

    return true;
}

bool SocketCAN::IsBusOff() const
{
    return m_busOff || m_txErrCnt > c_busOffThreshold;
}

bool SocketCAN::Send(uint32_t id, bool id29Bit, uint8_t dlc, const FramePayload& data)
{
    bool result = false;
    can_frame msg {};

    if ((c_invalidSocket != m_socket) && (dlc <= 8U) && !m_busOff) {
        msg.can_id = id;
        if (true == id29Bit) {
            msg.can_id |= CAN_EFF_FLAG;
        }
        msg.can_dlc = dlc;
        std::memcpy(msg.data, data.data(), dlc);
        auto res = write(m_socket, &msg, sizeof(struct can_frame));
        if (res < 0) {
            auto tempErrCode = errno;
            std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName << ": failed to write, error code " << tempErrCode
                      << std::endl;

            if (tempErrCode == EOVERFLOW || tempErrCode == ENOBUFS) {
                m_txErrCnt++;
            }
        } else {
            m_txErrCnt = 0;
        }
        result = (res >= 0);
    }

    return result;
}

bool SocketCAN::Receive(uint32_t& id, bool& id29Bit, uint8_t& dlc, FramePayload& data)
{
    timeval timeout;
    int ret;
    fd_set rfds;
    can_frame frame;

    timeout.tv_sec = 0;
    timeout.tv_usec = 100;

    if (c_invalidSocket == m_socket) {
        return false;
    }

    // Set up a file descriptor set only containing one socket
    FD_ZERO(&rfds);
    FD_SET(m_socket, &rfds);

    // Use select to be able to use a timeout
    ret = select(m_socket + 1, &rfds, nullptr, nullptr, &timeout);
    if (ret < 0) {
        auto tempErrCode = errno;
        std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName << ": failed to set read timeout, error code "
                  << tempErrCode << std::endl;
        return false;
    }

    if (!FD_ISSET(m_socket, &rfds)) {
        return false;
    }

    auto bytesRead = read(m_socket, &frame, sizeof(frame));
    if (bytesRead == sizeof(frame)) {
        id29Bit = ((frame.can_id & CAN_EFF_FLAG) > 0);
        UpdateStats(frame.can_dlc, bytesRead, id29Bit);
        const bool error = frame.can_id & CAN_ERR_FLAG;
        id = frame.can_id & (id29Bit ? CAN_EFF_MASK : CAN_SFF_MASK);
        if (!error) {
            dlc = frame.can_dlc;
            std::memcpy(data.data(), frame.data, dlc);
            m_txErrCnt = 0;
            m_busOff = false;
            return true;
        } else {
            std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName << ": frame error!\n"
                      << TranslateErrorFrame(frame) << std::endl;
            m_busOff = ((frame.can_id & CAN_ERR_BUSOFF)
                || ((frame.can_id & CAN_ERR_CRTL)
                    && (frame.data[1] & (CAN_ERR_CRTL_TX_PASSIVE | CAN_ERR_CRTL_RX_PASSIVE))));
        }
    } else if (bytesRead < 0) {
        auto tempErrCode = errno;
        std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName << ": failed to read, error code " << tempErrCode
                  << std::endl;
    }

    return false;
}

bool SocketCAN::Poll(const OnDataRXCallback& rxClbkFunc)
{
    int tempErrCode = 0;
    if (m_socket == c_invalidSocket)
        return false;

    int epollFd = epoll_create1(0);
    if (epollFd == c_invalidSocket) {
        tempErrCode = errno;
        std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName
                  << ": failed to allocate polling file descriptor, error code " << tempErrCode << std::endl;
        return false;
    }

    std::array<struct epoll_event, 30> events {};

    struct epoll_event ev { };
    ev.events = EPOLLIN;
    ev.data.fd = m_socket;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, m_socket, &ev) == -1) {
        tempErrCode = errno;
        std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName
                  << ": failed to configure polling file descriptor, error code " << tempErrCode << std::endl;
        close(epollFd);
        return false;
    }

    m_stopPolling.store(false);
    while (!m_stopPolling.load()) {
        int activeFds = epoll_wait(epollFd, events.data(), events.size(), 5);
        if (activeFds == -1) {
            tempErrCode = errno;
            continue;
        }

        for (size_t idx = 0; idx < activeFds && idx < events.size(); idx++) {
            if (events[idx].data.fd != m_socket)
                continue;

            struct can_frame rxFrame { };
            auto rxBytes = read(m_socket, &rxFrame, sizeof(rxFrame));
            if (rxBytes == -1) {
                tempErrCode = errno;
                continue;
            }

            if (rxBytes != sizeof(rxFrame)) // incomplete frame!
                continue;

            const bool error = rxFrame.can_id & CAN_ERR_FLAG;
            const bool id29Bit = rxFrame.can_id & CAN_EFF_FLAG;

            UpdateStats(rxFrame.can_dlc, rxBytes, id29Bit);
            if (!error) {
                FramePayload data {};
                std::copy(rxFrame.data, rxFrame.data + CAN_MAX_DLEN, data.begin());
                rxClbkFunc(rxFrame.can_id & (id29Bit ? CAN_EFF_MASK : CAN_SFF_MASK), id29Bit, rxFrame.can_dlc, data);
                m_busOff = false;
            } else {
                std::cerr << ERR_MARKER << LOG_MARKER << m_ifaceName << ": frame error!\n"
                          << TranslateErrorFrame(rxFrame) << std::endl;
                m_busOff = ((rxFrame.can_id & CAN_ERR_BUSOFF)
                    || ((rxFrame.can_id & CAN_ERR_CRTL)
                        && (rxFrame.data[1] & (CAN_ERR_CRTL_TX_PASSIVE | CAN_ERR_CRTL_RX_PASSIVE))));
            }
        }
    }
    return tempErrCode == 0;
}

int SocketCAN::BusLoad()
{
    int load = m_stats.Load(m_bitrate);
    m_stats.Reset();
    return load;
}

bool SocketCAN::SetBitrate(const int bitrate)
{
    nl_sock* sock = nullptr;
    nl_cache* cache = nullptr;
    rtnl_link* link = nullptr;
    bool ok = true;

    ok &= Netlink_Connect(sock, cache);
    ok &= Netlink_GetInterface(sock, cache, link, m_ifaceName);
    ok &= Netlink_BringDown(sock, link);
    ok &= Netlink_CANSetBitrate(sock, link, m_bitrate);
    ok &= Netlink_SetTXQueueLen(sock, link, 1000);
    Netlink_DisposeInterface(link);
    Netlink_Dispose(sock, cache);

    if (ok)
        m_bitrate = bitrate;

    return ok;
}

int SocketCAN::BusStats::Load(const int ifaceBitrate) const
{
    using namespace std::chrono;
    const auto msTimeDiff = duration_cast<milliseconds>(steady_clock::now() - lastStat).count();
    if (msTimeDiff <= 0)
        return 0;

    const auto msBitrate = ifaceBitrate / 1000.;
    const auto maxXferBits = msBitrate * msTimeDiff;
    if (maxXferBits <= 0)
        return 0;

    return rxBitsTotal * 100 / maxXferBits;
}

void SocketCAN::BusStats::Reset()
{
    rxCount = 0;
    rxBitsTotal = 0;
    rxBitsPayload = 0;
    lastStat = std::chrono::steady_clock::now();
}

unsigned long long SocketCAN::FrameBitLength(const bool id29Bit, const uint8_t dlc, const size_t mtu)
{
    // Adapted from https://github.com/linux-can/can-utils/blob/master/canframelen.c, picked WORSTCASE mode
    if (mtu == CANFD_MTU)
        return (1 + (id29Bit ? 29 : 11) + ((dlc >= 16) ? 21 : 17)
                   + 5 /* r1, ide, edl, r0, brs/crcdel, */ + 12 /* trail */ + dlc * 8)
            * 5 / 4;
    else if (mtu != CAN_MTU)
        return 0; /* Only CAN2.0 and CANFD supported now */

    return (id29Bit ? 80 : 55) + dlc * 10;
}

std::string SocketCAN::TranslateErrorFrame(const can_frame& frame)
{
    if (!(frame.can_id & CAN_ERR_FLAG)) // not an error
        return "";

    std::stringstream builder {};
    size_t errCnt = 0;
    for (const auto& [flag, str] : ErrorClassBits) {
        if (!(frame.can_id & flag))
            continue;

        if (errCnt > 0)
            builder << ", ";

        builder << str;
        switch (flag) {
        case CAN_ERR_LOSTARB: {
            builder << "lost arbitration on "
                    << (frame.data[0] == CAN_ERR_LOSTARB_UNSPEC ? "unknown" : std::to_string((int)frame.data[0]))
                    << " bit";
        } break;

        case CAN_ERR_CRTL: {
            const auto& detailByte = frame.data[1];
            if (detailByte == CAN_ERR_CRTL_UNSPEC) {
                builder << "unspecified controller fault";
                break;
            }

            size_t ctrlErrCnt = 0;
            builder << " controller fault [";
            for (const auto& [ctrlFlag, ctrlStr] : ControllerErrorStatus) {
                if (!(detailByte & ctrlFlag))
                    continue;

                if (ctrlErrCnt > 0)
                    builder << ", ";
                builder << ctrlStr;
                ctrlErrCnt++;
            }
            builder << ']';
        } break;

        case CAN_ERR_PROT: {
            const auto& detailByte = frame.data[2];
            const auto& locationByte = frame.data[3];
            if (detailByte == CAN_ERR_PROT_UNSPEC) {
                builder << "unspecified protocol violation";
                break;
            }

            size_t protErrCnt = 0;
            builder << " protocol violation [";
            for (const auto& [protTypeFlag, protTypeStr] : ProtocolErrorType) {
                if (!(detailByte & protTypeFlag))
                    continue;

                if (protErrCnt > 0)
                    builder << ", ";
                builder << protTypeStr;
                if (locationByte != CAN_ERR_PROT_LOC_UNSPEC) {
                    builder << " on " << ProtocolErrorLocation.at(locationByte);
                }
                protErrCnt++;
            }
            builder << ']';
        } break;

        case CAN_ERR_TRX: {
            const auto& detailByte = frame.data[4];
            if (detailByte == CAN_ERR_TRX_UNSPEC) {
                builder << "unspecified transceiver fault";
                break;
            }

            size_t trxErrCnt = 0;
            builder << " transceiver fault [";
            for (const auto& [trxFlag, trxStr] : TransceiverError) {
                if (!(detailByte & trxFlag))
                    continue;

                if (trxErrCnt > 0)
                    builder << ", ";
                builder << trxStr;
                trxErrCnt++;
            }
            builder << ']';
        } break;

        default: // this error class doesn't carry additional info
            break;
        }

        errCnt++;
    }
    return builder.str();
}

void SocketCAN::UpdateStats(uint8_t dlc, size_t mtu, bool id29Bit)
{
    m_stats.rxCount++;
    m_stats.rxBitsPayload += dlc * 8;
    m_stats.rxBitsTotal += FrameBitLength(id29Bit, dlc, mtu);
}
