#ifndef ZEPHYR_INCLUDE_DEVICE_H_
#define ZEPHYR_INCLUDE_DEVICE_H_

#include <zephyr/toolchain.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct device;

/* Device state */
struct device_state {
    uint8_t init_res;
    bool initialized : 1;
};

/* Device flags */
typedef uint8_t device_flags_t;
#define DEVICE_FLAG_INIT_DEFERRED BIT(0)

/* Device operations */
struct device_ops {
    int (*init)(const struct device *dev);
};

/* Device structure */
struct device {
    const char *name;
    const void *config;
    const void *api;
    struct device_state *state;
    void *data;
    struct device_ops ops;
    device_flags_t flags;
};

bool device_is_ready(const struct device *dev);
const struct device *device_get_binding(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DEVICE_H_ */
