# canopen-timers
A minimum viable demonstration of timer clobbering in certain multithreaded applications.

## Structure
It's quick and dirty, feel free to complain about a bad structure:
- `lib/`, simple libraries that should be able to work easily in other projects
  * `lib/canopen-stack/`, git submodule! Make sure to update your submodule references before compiling
  * `lib/socketcan/`, my simple SocketCAN wrapper, with some extra capabilities that happen to be useful when addressing communication problems on the field (eg, TX/RX failure due to bus-off or bad termination)
- `src/`, core app
  * `src/main.cpp`, main entrypoint, with launch arg parsing, soft closure on SIGINT (signal 15, aka CTRL+C) and basic application tick generator
  * `src/co_addr.hpp`, simple utilities to make my life easier when working with object indices
  * `src/co_can_linux.cpp`, SocketCAN abstraction layer
  * `src/co_nvm_linux.cpp`, probably-not-working non-volatile storage abstraction layer, never got to the point of testing it
  * `src/co_timer_linux.cpp`, timer abstraction layer, based around OS-provided timers. Originally implemented with SIGALRM, then moved to thread spawning
  * `src/mystack.cpp`, CANopen node structure lives here, including object dictionary and other important bits
  * `src/varloop.cpp`, the dumbest way I could think of for generating data to be sent out for TPDOs
  * `src/utils.hpp`, quick string and file system manipulation


## Prerequisites
A few basic packages are required:
- A C++17-capable compiler, `g++`
- A debugger, if you want to debug this thing, `gdb` (and/or `gdbserver`)
- Build tools, `cmake` and `ninja-build`
- Various netlink libraries and headers for letting the application self-configure the CAN interface:
  - `libnl-3-200` and `libnl-3-dev`
  - `libnl-route-3-200` and `libnl-route-3-dev`
  - `libnl-genl-3-200` and `libnl-genl-3-dev`

## Tested environments

Initial discovery occurred on Raspbian 10 Buster (kernel 4.19.115), on RPi CM3+ and MCP2515 CAN controllers. Compiler is GCC 8.3.0 armhf. Sources for this system will not be rendered available.

This example has been tested on WSL/Ubuntu 22.04.5 LTS (kernel 5.15.153.1-microsoft-standard-WSL2+) and a PEAK-USB dongle. using GCC 11.4.0 x86_64 and CMake using Ninja as makefile generator.\
WSL environment has been reconfigured for SocketCAN support using this snippet: https://gist.github.com/yonatanh20/664f07d62eb028db18aa98b00afae5a6 \
Normal Linux distros should work out-of-the-box, as long as they have the `can-raw` kernel module available.

All systems have been monitored using Vector CANalyzer on the other side of the bus.

## Behavior

1. CAN bus is initialized at 250 kb/s.
2. Node defaults to ID 10 (0x0A) and starts up immediately in Operational state.
3. TPDO #1 lives on COB-ID 0x18A and outputs 4 bytes with an incrementing 32-bit counter.
4. TPDO #2 lives on COB-ID 0x28A and outputs 4 bytes with a decrementing 32-bit counter and 4 bytes with a moving bit across 32 bits.
5. Values in TPDOs are updated every 500ms.
6. All variables are written to bus in little-endian format (least significant byte first).

**EXPECTING:** TPDOs should be transmitted on the bus every 250ms.\
**EFFECTIVE:** TPDOs may randomly stop transmitting. Switching between Operational and Pre-operational states may or may not restore functionality.

## Debugging notes

From what I've observed, it seems like there's a stray race condition somewhere, so with some very specific timing, it probably does stuff in the timer array while being interrupted by the ISR. A `std::mutex` is in place, and it's bound to the weakly-linked `COTmrLock/Unlock()` functions from the stack, so it should be already playing ball as canopen-stack wants.

I've been able to semi-reliably have it hang when running under `strace` with arg `-e 'trace=!nanosleep,clock_nanosleep'`. Usually, it drops dead in the matter of seconds, but occasionally it can survive for several minutes.

Example:
```
...
write(3, "\212\1\0\0\4\0\0\0@\0\0\0\0\0\0\0", 16) = 16
write(1, "D: [HAL::CAN] > TX 0x0000018A 40"..., 42D: [HAL::CAN] > TX 0x0000018A 40 00 00 00
) = 42
timer_gettime(0, {it_interval={tv_sec=0, tv_nsec=0}, it_value={tv_sec=0, tv_nsec=4250377}}) = 0
timer_gettime(0, {it_interval={tv_sec=0, tv_nsec=0}, it_value={tv_sec=0, tv_nsec=4017403}}) = 0
write(3, "\212\2\0\0\10\0\0\0\277\377\377\377\0\1\0\0", 16) = 16
write(1, "D: [HAL::CAN] > TX 0x0000028A BF"..., 54D: [HAL::CAN] > TX 0x0000028A BF FF FF FF 00 01 00 00
) = 54
timer_gettime(0, {it_interval={tv_sec=0, tv_nsec=0}, it_value={tv_sec=0, tv_nsec=244957014}}) = 0
timer_settime(0, 0, {it_interval={tv_sec=0, tv_nsec=0}, it_value={tv_sec=0, tv_nsec=5000000}}, NULL) = 0
timer_gettime(0, {it_interval={tv_sec=0, tv_nsec=0}, it_value={tv_sec=0, tv_nsec=4831471}}) = 0
write(3, "\212\1\0\0\4\0\0\0@\0\0\0\0\0\0\0", 16) = 16
write(1, "D: [HAL::CAN] > TX 0x0000018A 40"..., 42D: [HAL::CAN] > TX 0x0000018A 40 00 00 00
) = 42
timer_gettime(0, {it_interval={tv_sec=0, tv_nsec=0}, it_value={tv_sec=0, tv_nsec=0}}) = 0
timer_gettime(0, {it_interval={tv_sec=0, tv_nsec=0}, it_value={tv_sec=0, tv_nsec=0}}) = 0   <===== THIS IS SUSPICIOUS!
write(3, "\212\1\0\0\4\0\0\0@\0\0\0\0\0\0\0", 16) = 16
write(1, "D: [HAL::CAN] > TX 0x0000018A 40"..., 42D: [HAL::CAN] > TX 0x0000018A 40 00 00 00
) = 42

# no more unsolicited output here, so the timer ISR is no longer being called
```

In this state, the app is still processing messages and shows RX/TX activity when switching NMT state
```
write(1, "D: [HAL::CAN] > TX 0x0000018A 40"..., 42D: [HAL::CAN] > TX 0x0000018A 40 00 00 00
) = 42
write(1, "D: [HAL::CAN] < RX 0x00000000 80"..., 36D: [HAL::CAN] < RX 0x00000000 80 00
) = 36
write(1, "[Stack] Status transition! Opera"..., 58[Stack] Status transition! Operational -> Pre-operational
) = 58
write(1, "D: [HAL::CAN] < RX 0x00000000 01"..., 36D: [HAL::CAN] < RX 0x00000000 01 00
) = 36
timer_gettime(0, {it_interval={tv_sec=0, tv_nsec=0}, it_value={tv_sec=0, tv_nsec=0}}) = 0
timer_gettime(0, {it_interval={tv_sec=0, tv_nsec=0}, it_value={tv_sec=0, tv_nsec=0}}) = 0
write(1, "[Stack] Status transition! Pre-o"..., 58[Stack] Status transition! Pre-operational -> Operational
) = 58
```

Unfortunately, under `gdb`, it hangs usually after a couple of hours and there's nothing that really catches my eye, other than `mystack::m_tmrMem[]` showing all soft-timers with 0 ticks remaining and the stack never rearming the HAL timer.
