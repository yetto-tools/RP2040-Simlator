#include "peripherals/sio.h"

namespace rp2040 {

namespace {
enum : std::uint32_t {
    kCpuid       = 0x000,
    kGpioIn      = 0x004,
    kGpioHiIn    = 0x008,
    kGpioOut     = 0x010,
    kGpioOutSet  = 0x014,
    kGpioOutClr  = 0x018,
    kGpioOutXor  = 0x01C,
    kGpioOe      = 0x020,
    kGpioOeSet   = 0x024,
    kGpioOeClr   = 0x028,
    kGpioOeXor   = 0x02C,
};
}  // namespace

BusResult<std::uint32_t> Sio::bus_read(std::uint32_t offset, BusWidth) {
    switch (offset) {
        case kCpuid:   return {cpuid_, BusStatus::Ok};
        case kGpioIn:   return {gpio_.input_bits(), BusStatus::Ok};
        case kGpioHiIn: return {0u, BusStatus::Ok};
        case kGpioOut:  return {gpio_.driver_out(Gpio::kSio), BusStatus::Ok};
        case kGpioOe:   return {gpio_.driver_oe(Gpio::kSio), BusStatus::Ok};
        default:        return {0u, BusStatus::Ok};
    }
}

BusStatus Sio::bus_write(std::uint32_t offset, std::uint32_t value, BusWidth) {
    const std::uint32_t out = gpio_.driver_out(Gpio::kSio);
    const std::uint32_t oe = gpio_.driver_oe(Gpio::kSio);
    switch (offset) {
        case kGpioOut:    gpio_.driver_set_out(Gpio::kSio, value); break;
        case kGpioOutSet: gpio_.driver_set_out(Gpio::kSio, out | value); break;
        case kGpioOutClr: gpio_.driver_set_out(Gpio::kSio, out & ~value); break;
        case kGpioOutXor: gpio_.driver_set_out(Gpio::kSio, out ^ value); break;
        case kGpioOe:     gpio_.driver_set_oe(Gpio::kSio, value); break;
        case kGpioOeSet:  gpio_.driver_set_oe(Gpio::kSio, oe | value); break;
        case kGpioOeClr:  gpio_.driver_set_oe(Gpio::kSio, oe & ~value); break;
        case kGpioOeXor:  gpio_.driver_set_oe(Gpio::kSio, oe ^ value); break;
        default: break;  // CPUID / GPIO_IN / reserved: writes ignored
    }
    return BusStatus::Ok;
}

}  // namespace rp2040
