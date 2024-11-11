#include "mystack.hpp"
#include "co_can_linux.hpp"
#include "co_nvm_linux.hpp"
#include "co_timer_linux.hpp"
#include "utils.hpp"

#include <algorithm>
#include <iostream>

static const std::string LOG_MARKER { "[Stack] " };
static const std::string ERR_MARKER { "E: " };
static const std::string DBG_MARKER { "D: " };

mystack::mystack(const std::string& canIface)
{
    co_can_linux::SetCANInterface(canIface);
    m_hw.Can = &co_can_linux::CANDriver();
    m_hw.Timer = &co_timer_linux::TimerDriver();
    m_hw.Nvm = &co_nvm_linux::NVMDriver();

    AllocateObjects();

    m_spec.NodeId = 10; /* default Node-Id */
    m_spec.Baudrate = 250000; /* default Baudrate */
    m_spec.Dict = m_dict.data(); /* pointer to object dictionary */
    m_spec.DictLen = (uint16_t)m_dict.size(); /* object dictionary max length */
    // m_spec.EmcyCode = m_emcyTbl.data(); /* EMCY code & register bit table */
    m_spec.EmcyCode = nullptr;
    m_spec.TmrMem = m_tmrMem.data(); /* pointer to timer memory blocks */
    m_spec.TmrNum = (uint16_t)m_tmrMem.size(); /* number of timer memory blocks  */
    m_spec.TmrFreq = 500000; // 500 kHz clock rate
    m_spec.Drv = &m_hw; /* select drivers for application */
    m_spec.SdoBuf = m_sdoSwap.data(); /* SDO Transfer Buffer Memory */

    CONodeInit(&m_node, &m_spec);
    if (const auto initRc = CONodeGetErr(&m_node); initRc != CO_ERR_NONE) {
        std::cerr << ERR_MARKER << LOG_MARKER << "CANopen stack initialization failed with error code " << initRc
                  << std::endl;
    }
}

void mystack::NodeStart()
{
    std::cout << LOG_MARKER << "Starting CANopen node" << std::endl;
    co_timer_linux::LinkTimer(&m_node.Tmr);
    CONodeStart(&m_node);
    CONmtSetMode(&m_node.Nmt, CO_OPERATIONAL);
}

void mystack::NodeTick()
{
    const auto currMode = CONmtGetMode(&m_node.Nmt);
    if (currMode != m_lastMode) {
        std::cout << LOG_MARKER << "Status transition! " << NodeModeStr(m_lastMode) << " -> " << NodeModeStr(currMode)
                  << std::endl;
        m_lastMode = currMode;
    }

    std::scoped_lock dataGuard(m_dataMtx);
    CONodeProcess(&m_node);
    COTmrProcess(&m_node.Tmr);
}

void mystack::NodeStop()
{
    std::cout << LOG_MARKER << "Stopping CANopen node" << std::endl;
    CONodeStop(&m_node);
}

void mystack::TriggerTPDO(const ObjectAddress& objAddr)
{
    auto obj = CODictFind(&m_node.Dict, CO_DEV(objAddr.Index(), objAddr.Subindex()));
    if (!obj)
        return;
    COTPdoTrigObj(m_node.TPdo, obj);
}

mystack::~mystack()
{
    NodeStop();
}

std::string mystack::NodeModeStr(const CO_MODE m)
{
    switch (m) {
    case CO_INIT:
        return "Init";
    case CO_PREOP:
        return "Pre-operational";
    case CO_OPERATIONAL:
        return "Operational";
    case CO_STOP:
        return "Stopped";
    default:
        return "Unknown?";
    }
}

static constexpr uint32_t TPDOMappedObject(const ObjectAddress& addr, const uint8_t size)
{
    return CO_LINK(addr.Index(), addr.Subindex(), size);
}

void mystack::AllocateObjects()
{
    // No standardized device profile
    AddObject<uint32_t>(Addresses::Std_DeviceType, CO_OBJ_____R_, CO_TUNSIGNED32, 0x00000000);

    // We could probably use the error bit, then tap into 0x1002 for extended status
    AddObject<uint8_t>(Addresses::Std_ErrorRegister, CO_OBJ_____R_, CO_TUNSIGNED8, 0x00);

    // Handled natively by CANopen library
    AddObject<CO_OBJ_HB_PROD>(Addresses::Std_HeartbeatProducerTime, CO_OBJ_____RW, CO_THB_PROD);

    // Count of subindices handled by identity object
    AddObject<uint8_t>(Addresses::Std_IdentityMaxSubindex, CO_OBJ_D___R_, CO_TUNSIGNED8, 4);
    AddObject<uint32_t>(Addresses::Std_IdentityVendorID, CO_OBJ_____R_, CO_TUNSIGNED32, 0);
    AddObject<uint32_t>(Addresses::Std_IdentityDeviceID, CO_OBJ_____R_, CO_TUNSIGNED32, 0);
    AddObject<uint32_t>(Addresses::Std_IdentityDeviceRev, CO_OBJ_____R_, CO_TUNSIGNED32, 0);
    AddObject<uint32_t>(Addresses::Std_IdentityDeviceSN, CO_OBJ_____R_, CO_TUNSIGNED32, 0);

    // The application supports SDO, so we are now declaring our COB IDs. Stack is nice and does most of the job for us
    AddObject<uint8_t>(Addresses::Std_SDOServerParam(0), CO_OBJ_D___R_, CO_TUNSIGNED8, 2);
    AddObject<uint32_t>(Addresses::Std_SDOServerRequestCOBID(0), CO_OBJ__N__R_, CO_TUNSIGNED32, CO_COBID_SDO_REQUEST());
    AddObject<uint32_t>(
        Addresses::Std_SDOServerResponseCOBID(0), CO_OBJ__N__R_, CO_TUNSIGNED32, CO_COBID_SDO_RESPONSE());

    // The application supports TPDO, so we are now declaring our COB IDs. Stack is nice and does most of the job for
    // us. Adding a quick wrapper function for cleaner code
    DefineTPDO(0, 0xFE, 50, 250,
        {
            { Addresses::App_Data1, 32 },
        });
    DefineTPDO(1, 0xFE, 50, 250,
        {
            { Addresses::App_Data2, 32 },
            { Addresses::App_Data3, 32 },
        });

    AddObject<uint32_t>(Addresses::App_Data1, CO_OBJ____PR_, CO_TUNSIGNED32, 0);
    AddObject<uint32_t>(Addresses::App_Data2, CO_OBJ____PR_, CO_TUNSIGNED32, 0);
    AddObject<uint32_t>(Addresses::App_Data3, CO_OBJ____PR_, CO_TUNSIGNED32, 0);

    // Stack relies on a binary tree algorithm for quickly finding the correct objects, so the dictionary must be
    // sorted!
    std::sort(m_dict.begin(), m_dict.end(), [](const CO_OBJ_T& a, const CO_OBJ_T& b) { return a.Key < b.Key; });

#ifndef NDEBUG
    DumpMemoryMap();
#endif
}

#ifndef NDEBUG
void mystack::DumpMemoryMap() const
{
    std::cout << DBG_MARKER << LOG_MARKER << "Current register map [" << m_objStorage.size() << "]\n";
    for (const auto& [addr, ptr] : m_objStorage) {
        std::cout << "  * " << addr << " -> " << ptr.get() << " = " << utils::ToHex(*(uint32_t*)(ptr.get()), true)
                  << "\n";
    }
    std::cout << std::endl;
}
#endif

void mystack::DefineTPDO(const uint16_t index, const uint8_t eventType, const uint16_t inhibitTime,
    const uint16_t triggerPeriod, const std::list<std::pair<ObjectAddress, uint8_t>>& objects)
{
    DefineTPDOParameters(index, eventType, inhibitTime, triggerPeriod);
    DefineTPDOMapping(index, objects);
}

void mystack::DefineTPDOParameters(
    const uint16_t index, const uint8_t eventType, const uint16_t inhibitTime, const uint16_t triggerPeriod)
{
    AddObject<uint8_t>(Addresses::Std_TPDOCommParam(index), CO_OBJ_D___R_, CO_TUNSIGNED8, 5);
    AddObject<uint32_t>(
        Addresses::Std_TPDOCommCOBID(index), CO_OBJ_DN__R_, CO_TUNSIGNED32, CO_COBID_TPDO_DEFAULT(index));
    AddObject<uint8_t>(Addresses::Std_TPDOCommType(index), CO_OBJ_D___R_, CO_TUNSIGNED8, eventType);
    AddObject<uint16_t>(Addresses::Std_TPDOCommInhibit(index), CO_OBJ_D___R_, CO_TUNSIGNED16, inhibitTime);
    AddObject<uint16_t>(Addresses::Std_TPDOCommTimer(index), CO_OBJ_D___R_, CO_TPDO_EVENT, triggerPeriod);
}

void mystack::DefineTPDOMapping(const uint16_t index, const std::list<std::pair<ObjectAddress, uint8_t>>& objects)
{
    size_t totBitWidth = 0;
    uint8_t totObjCount = 0;
    auto currObj = objects.begin();
    while (totBitWidth < 64 && currObj != objects.end()) {
        if (totBitWidth + currObj->second <= 64) {
            totBitWidth += currObj->second;
            totObjCount++;
            AddObject<uint32_t>(Addresses::Std_TPDOMappingSize(index) + totObjCount, CO_OBJ_D___R_, CO_TUNSIGNED32,
                TPDOMappedObject(currObj->first, currObj->second));
        } else {
            std::cerr << "W: " << LOG_MARKER << "Dropping object " << currObj->first << " from TPDO map #" << index
                      << std::endl;
        }
        currObj++;
    }
    AddObject<uint8_t>(Addresses::Std_TPDOMappingSize(index), CO_OBJ_D___R_, CO_TPDO_NUM, totObjCount);
}
