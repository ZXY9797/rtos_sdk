#pragma once

#include <algo/thermal/thermal_controller.h>

namespace gimbal {

using algo::thermal::ThermalConfig;
using algo::thermal::ThermalFault;
using algo::thermal::ThermalMode;
using algo::thermal::ThermalOutput;
using algo::thermal::valid_thermal_config;
using ImuThermalController = algo::thermal::ThermalController;

} // namespace gimbal
