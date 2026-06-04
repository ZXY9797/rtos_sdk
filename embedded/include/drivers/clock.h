#pragma once

#include <cstdint>
#include <errno.h>

namespace hal {

struct ClockConfig {
    uint32_t bus;
    uint32_t enr;
};

class ClockCtrlBase {
public:
    explicit ClockCtrlBase(uintptr_t base) : base_(base) {}

    [[nodiscard]] int enable(const ClockConfig& cfg);
    [[nodiscard]] int disable(const ClockConfig& cfg);

protected:
    uintptr_t base_;
};

template <uintptr_t Base>
class ClockCtrl : public ClockCtrlBase {
public:
    ClockCtrl() : ClockCtrlBase(Base) {}
};

} // namespace hal
