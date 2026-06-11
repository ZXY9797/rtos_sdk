#include <pinctrl.h>

namespace hal {
namespace pinctrl {

Status apply(const Operation *ops, std::size_t count) {
    if (!ops && count != 0U) {
        return Status::InvalidArgument;
    }

    for (std::size_t i = 0; i < count; ++i) {
        const Operation &op = ops[i];
        auto *reg = reinterpret_cast<volatile uint32_t *>(op.address);

        switch (op.type) {
            case OperationType::Rmw:
                *reg = (*reg & ~op.clear_mask) | op.set_value;
                break;
            case OperationType::Write:
                *reg = op.set_value;
                break;
            default:
                return Status::InvalidArgument;
        }
    }

    return Status::Ok;
}

} // namespace pinctrl
} // namespace hal
