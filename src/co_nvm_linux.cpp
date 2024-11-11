#include "co_nvm_linux.hpp"
#include "utils.hpp"

#include <iomanip>
#include <iostream>
#include <vector>

static const std::string LOG_MARKER { "[HAL::NVM] " };
static const std::string ERR_MARKER { "E: " };
static const std::string DBG_MARKER { "D: " };

const CO_IF_NVM_DRV& co_nvm_linux::NVMDriver()
{
    return s_coNvmDrv;
}

const CO_IF_NVM_DRV co_nvm_linux::s_coNvmDrv {
    co_nvm_linux::Init,
    co_nvm_linux::Read,
    co_nvm_linux::Write,
};

const std::string co_nvm_linux::c_nvmFilePath { "./kconvm.dat" };

std::fstream co_nvm_linux::s_nvmFile {};

void co_nvm_linux::Init()
{
    std::cout << LOG_MARKER << "Using " << std::quoted(c_nvmFilePath) << std::endl;
    s_nvmFile.open(c_nvmFilePath, std::ios::binary | std::ios::in | std::ios::out);
}

uint32_t co_nvm_linux::Read(uint32_t start, uint8_t* buffer, uint32_t size)
{
    // File not open
    if (!s_nvmFile) {
        std::cerr << ERR_MARKER << LOG_MARKER << "Failed to read, file not ready" << std::endl;
        return 0;
    }

    s_nvmFile.seekg(start, std::ios::beg);
    // Failed to seek
    if (!s_nvmFile) {
        std::cerr << ERR_MARKER << LOG_MARKER << "Failed to read, seek failed for offset " << start << std::endl;
        return 0;
    }

    s_nvmFile.read(reinterpret_cast<char*>(buffer), size);
    const auto readBytes = s_nvmFile.gcount();
#ifndef NDEBUG
    std::cout << DBG_MARKER << LOG_MARKER << "Read: off " << start << ", req " << size << ", read " << readBytes
              << std::endl;
#endif
    return readBytes;
}

uint32_t co_nvm_linux::Write(uint32_t start, uint8_t* buffer, uint32_t size)
{
    // File not open
    if (!s_nvmFile) {
        std::cerr << ERR_MARKER << LOG_MARKER << "Failed to write, file not ready" << std::endl;
        return 0;
    }

    const auto currSize = utils::GetFileSize(c_nvmFilePath);
    if (currSize < start) {
#ifndef NDEBUG
        std::cout << DBG_MARKER << LOG_MARKER << "Write: filling " << start - currSize << " bytes first" << std::endl;
#endif
        s_nvmFile.seekp(0, std::ios::end);
        std::vector<char> filler {};
        filler.resize(start - currSize, 0x00);
        s_nvmFile.write(filler.data(), filler.size());
    }

    s_nvmFile.seekp(start, std::ios::beg);
    // Failed to seek
    if (!s_nvmFile) {
        std::cerr << ERR_MARKER << LOG_MARKER << "Failed to write, seek failed for offset " << start << std::endl;
        return 0;
    }

    s_nvmFile.write(reinterpret_cast<char*>(buffer), size);
    const auto writeOff = (uint)s_nvmFile.tellp();
    const auto writeBytes = writeOff - start;
    s_nvmFile.flush();
#ifndef NDEBUG
    std::cout << DBG_MARKER << LOG_MARKER << "Write: off " << start << ", req " << size << ", written " << writeBytes
              << std::endl;
#endif
    return writeBytes;
}
