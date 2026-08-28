#pragma once

// Host header-contract tests do not execute or model target interrupt state.
#ifdef __cplusplus
namespace hal {

class IrqGuard {
public:
    IrqGuard();
    ~IrqGuard();
    IrqGuard(const IrqGuard&) = delete;
    IrqGuard& operator=(const IrqGuard&) = delete;
};

} // namespace hal
#endif
