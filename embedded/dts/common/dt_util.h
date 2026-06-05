/*
 * Copyright (c) 2018 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DTS_COMMON_DT_UTIL_H_
#define ZEPHYR_DTS_COMMON_DT_UTIL_H_

#define DT_SIZE_K(x) ((x) * 1024)
#define DT_SIZE_M(x) (DT_SIZE_K(x) * 1024)

#define DT_FREQ_K(x) ((x) * 1000)
#define DT_FREQ_M(x) (DT_FREQ_K(x) * 1000)

/* concatenate the values of the arguments into one */
#define _DT_DO_CONCAT(x, y) x ## y

#define DT_ADDR(a) _DT_DO_CONCAT(0x, a)

#endif /* ZEPHYR_DTS_COMMON_DT_UTIL_H_ */
