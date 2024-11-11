#include "varloop.hpp"
#include "co_addr.hpp"

varloop::varloop(mystack& coStack)
    : m_coStack(coStack)
{
}

void varloop::Tick()
{
    if (std::chrono::steady_clock::now() > m_lastUpdate + TickRate) {
        m_dataPoint1++;
        m_dataPoint2--;
        if (m_dataPoint3 != 0x8000000)
            m_dataPoint3 *= 2;
        else
            m_dataPoint3 = 1;

        m_coStack.SetObject(Addresses::App_Data1, m_dataPoint1);
        m_coStack.SetObject(Addresses::App_Data2, m_dataPoint2);
        m_coStack.SetObject(Addresses::App_Data3, m_dataPoint3);

        m_lastUpdate = std::chrono::steady_clock::now();
    }
}