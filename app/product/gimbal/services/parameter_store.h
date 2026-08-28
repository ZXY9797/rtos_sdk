#pragma once

#include <gimbal/parameters.h>

#include <cstdint>

namespace app {

class ParameterStore {
public:
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool load(gimbal::FactoryParameters &factory,
                            gimbal::UserParameters &user);
    [[nodiscard]] bool save_factory(
        const gimbal::FactoryParameters &factory);
    [[nodiscard]] bool save_user(
        const gimbal::UserParameters &user,
        const gimbal::FactoryParameters &factory);
    [[nodiscard]] bool factory_valid() const { return factory_valid_; }
    [[nodiscard]] bool user_valid() const { return user_valid_; }

private:
    uint32_t factory_generation_ {0U};
    uint32_t user_generation_ {0U};
    bool initialized_ {false};
    bool factory_valid_ {false};
    bool user_valid_ {false};
};

} // namespace app
