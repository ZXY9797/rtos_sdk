#pragma once

#include <algo/spatial_math.h>
#include <gimbal/types.h>

namespace gimbal {
namespace math = algo::spatial;
}

// Compatibility include for existing gimbal-domain users. New reusable code
// should include <algo/spatial_math.h> and use algo::spatial directly.
