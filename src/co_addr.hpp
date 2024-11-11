#ifndef CANOPEN_TIMERS_SRC_CO_ADDR_HPP_
#define CANOPEN_TIMERS_SRC_CO_ADDR_HPP_

#include <algorithm>
#include <cstdint>
#include <iostream>

struct ObjectAddress {
    ObjectAddress(uint16_t index, uint8_t subidx)
        : fullRef((index << 16) | (subidx << 8)) {};

    constexpr uint16_t Index() const
    {
        return static_cast<uint16_t>((fullRef & 0xFFFF0000) >> 16);
    }

    constexpr uint8_t Subindex() const
    {
        return static_cast<uint8_t>((fullRef & 0x0000FF00) >> 8);
    }

    inline bool operator<(const ObjectAddress& that) const
    {
        return this->fullRef < that.fullRef;
    }

    friend ObjectAddress operator+(const ObjectAddress& that, const int subidx)
    {
        if (that.Subindex() != 0)
            return ObjectAddress(that);

        ObjectAddress newAddr(that.Index(), that.Subindex() + subidx);
        return newAddr;
    }

    friend std::ostream& operator<<(std::ostream& os, const ObjectAddress& that)
    {
        os << std::hex << that.Index() << ":" << (uint)that.Subindex() << std::dec;
        return os;
    }

private:
    uint32_t fullRef { 0 };
};

namespace Addresses {
static const ObjectAddress Std_DeviceType { 0x1000, 0x00 }; // RO u32
static const ObjectAddress Std_ErrorRegister { 0x1001, 0x00 }; // RO u8
static const ObjectAddress Std_HeartbeatProducerTime { 0x1017, 0x00 }; // RW CO_OBJ_HB_PROD
static const ObjectAddress Std_IdentityMaxSubindex { 0x1018, 0x00 }; // RO u8
static const ObjectAddress Std_IdentityVendorID { 0x1018, 0x01 }; // RO u32
static const ObjectAddress Std_IdentityDeviceID { 0x1018, 0x02 }; // RO u32
static const ObjectAddress Std_IdentityDeviceRev { 0x1018, 0x03 }; // RO u32
static const ObjectAddress Std_IdentityDeviceSN { 0x1018, 0x04 }; // RO u32

static const ObjectAddress App_Data1 { 0x2000, 0x00 }; // RO s24
static const ObjectAddress App_Data2 { 0x2002, 0x00 }; // RO u8
static const ObjectAddress App_Data3 { 0x2010, 0x00 }; // RO u32
static const ObjectAddress App_Data4 { 0x2011, 0x00 }; // RO u32

inline static ObjectAddress Std_SDOServerParam(int num) // RO u8
{
    num = std::clamp(num, 0, 127);
    return { static_cast<uint16_t>(0x1200 + num), 0x00 };
}

inline static ObjectAddress Std_SDOServerRequestCOBID(int num) // RO u32
{
    num = std::clamp(num, 0, 127);
    return { static_cast<uint16_t>(0x1200 + num), 0x01 };
}

inline static ObjectAddress Std_SDOServerResponseCOBID(int num) // RO u32
{
    num = std::clamp(num, 0, 127);
    return { static_cast<uint16_t>(0x1200 + num), 0x02 };
}

inline static ObjectAddress Std_TPDOCommParam(int num) // RO u8
{
    num = std::clamp(num, 0, 511);
    return { static_cast<uint16_t>(0x1800 + num), 0x00 };
}

inline static ObjectAddress Std_TPDOCommCOBID(int num) // RO u32
{
    num = std::clamp(num, 0, 511);
    return { static_cast<uint16_t>(0x1800 + num), 0x01 };
}

inline static ObjectAddress Std_TPDOCommType(int num) // RO u32
{
    num = std::clamp(num, 0, 511);
    return { static_cast<uint16_t>(0x1800 + num), 0x02 };
}

inline static ObjectAddress Std_TPDOCommInhibit(int num) // RO u16
{
    num = std::clamp(num, 0, 511);
    return { static_cast<uint16_t>(0x1800 + num), 0x03 };
}

inline static ObjectAddress Std_TPDOCommTimer(int num) // RO u16
{
    num = std::clamp(num, 0, 511);
    return { static_cast<uint16_t>(0x1800 + num), 0x05 };
}

inline static ObjectAddress Std_TPDOMappingSize(int num) // RO u8
{
    num = std::clamp(num, 0, 511);
    return { static_cast<uint16_t>(0x1A00 + num), 0x00 };
}

};

#endif // CANOPEN_TIMERS_SRC_CO_ADDR_HPP_
