#ifndef CANOPEN_TIMERS_SRC_UTILS_HPP_
#define CANOPEN_TIMERS_SRC_UTILS_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <type_traits>

#include <sys/stat.h>

namespace utils {
template <typename T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>, bool> = true>
static inline std::string ToHex(const T& value, bool prefix = false)
{
    std::string result { "" };
    if (prefix)
        result += "0x";
    result.reserve(result.length() + (sizeof(value) * 2));
    static constexpr char hex[] = "0123456789ABCDEF";
    for (ssize_t shift = sizeof(value) - 1; shift >= 0; shift--) {
        uint8_t preppedByte = 0;
        if constexpr (std::is_enum_v<T>) {
            preppedByte = (static_cast<std::underlying_type_t<T>>(value) & (0xFF << (shift * 8))) >> (shift * 8);
        } else {
            preppedByte = (value & (0xFF << (shift * 8))) >> (shift * 8);
        }
        result.push_back(hex[preppedByte >> 4]);
        result.push_back(hex[preppedByte & 0xf]);
    }
    return result;
}

static inline std::string& TrimLeft(std::string& s)
{
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), [](int c) { return !isgraph(c); }));
    return s;
}

static inline std::string& TrimRight(std::string& s)
{
    s.erase(std::find_if_not(s.rbegin(), s.rend(), [](int c) { return !isgraph(c); }).base(), s.end());
    return s;
}

static inline std::string Trim(const std::string& s)
{
    std::string t = s;
    return TrimLeft(TrimRight(t));
}

inline std::vector<std::string> Split(
    const std::string& input, const std::string& separator, bool ignoreBlankStr = true)
{
    std::string workStr = input;
    std::vector<std::string> outputVect {};

    if (separator.length() == 1) {
        std::istringstream iss(workStr);
        std::string token;
        while (std::getline(iss, token, separator[0])) {
            token = Trim(token);
            if (token.length() > 0 || ignoreBlankStr == false)
                outputVect.push_back(token);
        }
    } else if (separator.length() > 1) {
        size_t pos = 0;
        std::string token;
        while ((pos = workStr.find(separator)) != std::string::npos) {
            token = workStr.substr(0, pos);
            token = Trim(token);
            if (token.length() > 0 || ignoreBlankStr == false)
                outputVect.push_back(token);
            workStr.erase(0, pos + separator.length());
        }
        if (workStr.length() > 0) {
            token = workStr;
            token = Trim(token);
            outputVect.push_back(workStr);
        }
    }

    return outputVect;
}

static inline std::string DumpBuffer(const uint8_t* ptr, const size_t len, const bool flip = false)
{
    std::string out = "";

    if (!flip) {
        for (size_t idx = 0; idx < len; idx++) {
            out += ToHex(ptr[idx]) + " ";
        }
    } else {
        for (ssize_t idx = len - 1; idx >= 0; idx--) {
            out += ToHex(ptr[idx]) + " ";
        }
    }

    return Trim(out);
}

static inline size_t GetFileSize(const std::string& filePath)
{
    struct stat fileStats { };
    if (stat(filePath.c_str(), &fileStats) == 0) {
        return fileStats.st_size;
    }
    return 0;
}

};

#endif // CANOPEN_TIMERS_SRC_UTILS_HPP_