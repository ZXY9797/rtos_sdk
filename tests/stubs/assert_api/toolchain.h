#pragma once

// Assertion ABI tests exercise the public declaration/macros, independent of
// the MCU compiler's format attribute and instruction helpers.
#define __printf_like(format_index, first_argument)
