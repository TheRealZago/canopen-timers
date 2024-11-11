#ifndef CANOPEN_TIMERS_SRC_CO_NVM_LINUX_HPP_
#define CANOPEN_TIMERS_SRC_CO_NVM_LINUX_HPP_

#include "co_if_nvm.h"

#include <fstream>
#include <string>

class co_nvm_linux {
public:
    static const CO_IF_NVM_DRV& NVMDriver();

private:
    static const CO_IF_NVM_DRV s_coNvmDrv;
    static const std::string c_nvmFilePath;

    static std::fstream s_nvmFile;

    static void Init();
    static uint32_t Read(uint32_t start, uint8_t* buffer, uint32_t size);
    static uint32_t Write(uint32_t start, uint8_t* buffer, uint32_t size);

    // Make it purely static
    co_nvm_linux() = delete;
    ~co_nvm_linux() = delete;
};

#endif // CANOPEN_TIMERS_SRC_CO_NVM_LINUX_HPP_
