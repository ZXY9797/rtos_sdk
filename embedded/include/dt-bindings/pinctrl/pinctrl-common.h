#ifndef EMBEDDED_INCLUDE_DT_BINDINGS_PINCTRL_PINCTRL_COMMON_H_
#define EMBEDDED_INCLUDE_DT_BINDINGS_PINCTRL_PINCTRL_COMMON_H_

#define PINCTRL_OP_RMW   0U
#define PINCTRL_OP_WRITE 1U
#define PINCTRL_OP_CELLS 4U

#define PINCTRL_OP(type, address, clear_mask, set_value) \
    (type) (address) (clear_mask) (set_value)

#endif /* EMBEDDED_INCLUDE_DT_BINDINGS_PINCTRL_PINCTRL_COMMON_H_ */
