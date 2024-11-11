#include "mystack.hpp"
#include "utils.hpp"
#include "varloop.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <list>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

const std::string LOG_MARKER { "[Main] " };
const std::string ERR_MARKER { "E: " };
const std::string DBG_MARKER { "D: " };

std::atomic_bool reqExit { false };

void SignalHandler(int sigNum)
{
    (void)sigNum;
    std::cout << LOG_MARKER << "Requesting exit..." << std::endl;
    reqExit.store(true);
}

void PrintVersion()
{
    std::cout << "A minimum viable application for simulating timer clobbering using canopen-stack" << std::endl;
}

void PrintInfo()
{
    PrintVersion();

    std::cout << "\n"
              << "     --iface=<port>    CAN interface to be used (eg, `can0')\n"
              << "\n"
              << "          --version    Print program version and exit\n"
              << "             --help    Print this help and exit\n"
              << std::endl;
}

std::pair<std::string, std::string> SplitArgument(const std::string& arg)
{
    std::vector<std::string> argParts {};
    std::stringstream sstream { arg };
    std::string temp { "" };

    while (std::getline(sstream, temp, '=')) {
        argParts.push_back(temp);
    }

    if (argParts.size() > 1) {
        return { argParts[0], argParts[1] };
    } else {
        return { arg, "" };
    }
}

void ParseArguments(int argc, char const* argv[], std::map<std::string, std::string, std::less<>>& output)
{
    static const std::list<std::string> validArgs {
        "--iface",
        "--help",
        "--version",
    };

    while (argc > 0) {
        argc--;
        std::string strArg(argv[argc]);

        auto splitBySpace = utils::Split(strArg, " ");
        for (const auto& singleArg : splitBySpace) {
            auto [key, val] = SplitArgument(singleArg);

            auto okArg = std::find(validArgs.begin(), validArgs.end(), key);
            if (okArg == validArgs.end()) {
                continue;
            }

            output[*okArg] = val;

            if (*okArg == "--help") {
                PrintInfo();
                exit(0);
            }

            if (*okArg == "--version") {
                PrintVersion();
                exit(0);
            }
        }
    }
}

int main(int argc, char const* argv[])
{
    std::cout << "canopen-timers - Enrico Zaghini - 2024" << std::endl;
    signal(SIGINT, SignalHandler);

    std::map<std::string, std::string, std::less<>> launchArgs {};
    ParseArguments(argc, argv, launchArgs);

    std::string canIface { "can0" };
    if (launchArgs.count("--iface") > 0) {
        canIface = launchArgs.at("--iface");
    } else {
        std::cerr << ERR_MARKER << LOG_MARKER << "Missing CAN interface argument (`--iface=...')!" << std::endl;
        PrintInfo();
        return 1;
    }

    mystack coStack { canIface };
    varloop loop { coStack };
    static constexpr auto LoopTiming = std::chrono::microseconds(500);
    coStack.NodeStart();
    while (!reqExit.load()) {
        const auto retrigger = std::chrono::steady_clock::now() + LoopTiming;
        coStack.NodeTick();
        loop.Tick();
        std::this_thread::sleep_until(retrigger);
    }

    return 0;
}