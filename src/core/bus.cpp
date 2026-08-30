#include "core/bus.h"

namespace rp2040 {

const char* to_string(BusStatus status) {
    switch (status) {
        case BusStatus::Ok:               return "Ok";
        case BusStatus::MisalignedAccess: return "MisalignedAccess";
        case BusStatus::InvalidAddress:   return "InvalidAddress";
        case BusStatus::WriteToReadOnly:  return "WriteToReadOnly";
        case BusStatus::PeripheralError:  return "PeripheralError";
    }
    return "?";
}

}  // namespace rp2040
