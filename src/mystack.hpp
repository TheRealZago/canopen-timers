#ifndef CANOPEN_TIMERS_SRC_MYSTACK_HPP_
#define CANOPEN_TIMERS_SRC_MYSTACK_HPP_

#include "co_addr.hpp"
#include "co_core.h"
#include "co_err.h"
#include "co_nmt.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

// C++ wrapper for the main C library
class mystack {
public:
    explicit mystack(const std::string& canIface);
    ~mystack();

    void NodeStart();
    void NodeTick();
    void NodeStop();

    template <typename T>
    void SetObject(const ObjectAddress& objAddr, T value)
    {
        // Type checking and sizeof comparison are done directly within COObjWrValue, so the stack should be able
        // to filter out if I'm doing dumb things when updating values
        std::scoped_lock dataGuard(m_dataMtx);
        auto obj = CODictFind(&m_node.Dict, CO_DEV(objAddr.Index(), objAddr.Subindex()));
        if (!obj)
            return;
        COObjWrValue(obj, &m_node, &value, sizeof(T));
    }
    void TriggerTPDO(const ObjectAddress& objAddr);

private:
    static constexpr size_t EmergencyCodeCount { 1 };
    static constexpr size_t TimersCount { 64 };

    CO_NODE m_node {};
    struct CO_IF_DRV_T m_hw { };
    struct CO_NODE_SPEC_T m_spec { };
    std::vector<CO_OBJ_T> m_dict {};
    std::array<CO_EMCY_TBL, EmergencyCodeCount> m_emcyTbl {};
    std::array<CO_TMR_MEM, TimersCount> m_tmrMem {};
    std::array<uint8_t, CO_SSDO_N * CO_SDO_BUF_BYTE> m_sdoSwap {};
    CO_MODE m_lastMode { CO_INVALID };
    std::map<ObjectAddress, std::shared_ptr<void>> m_objStorage {};
    std::mutex m_dataMtx {};

    template <typename T>
    void AddObject(
        const ObjectAddress& objAddr, const uint8_t flags, const CO_OBJ_TYPE* coDataType, const T& defaultValue = {})
    {
        // Allocate object only if not explictly marked as stored by internal stack buffers
        if (CO_IS_DIRECT(flags)) {
            m_dict.push_back({
                CO_KEY(objAddr.Index(), objAddr.Subindex(), flags),
                coDataType,
                (CO_DATA)(defaultValue),
            });
        } else {
            m_objStorage[objAddr] = std::make_shared<T>(defaultValue);
            m_dict.push_back({
                CO_KEY(objAddr.Index(), objAddr.Subindex(), flags),
                coDataType,
                (CO_DATA)(m_objStorage[objAddr].get()),
            });
        }
    }

    static std::string NodeModeStr(const CO_MODE m);

    void AllocateObjects();
    void DumpMemoryMap() const;
    void DefineTPDO(const uint16_t index, const uint8_t eventType, const uint16_t inhibitTime,
        const uint16_t triggerPeriod, const std::list<std::pair<ObjectAddress, uint8_t>>& objects);
    void DefineTPDOParameters(
        const uint16_t index, const uint8_t eventType, const uint16_t inhibitTime, const uint16_t triggerPeriod);
    void DefineTPDOMapping(const uint16_t index, const std::list<std::pair<ObjectAddress, uint8_t>>& objects);
};

#endif // CANOPEN_TIMERS_SRC_MYSTACK_HPP_
