#include "comm/motor_protocol.h"
#include <cmath>
#include <cstring>
#include <type_traits>

namespace app
{
bool encode_interaction_section(InteractionConfig c, uint8_t section, MotorResponse &r)
{
    r = {};
    r.byte(1);
    r.byte(section);
    r.u16(0);
    unsigned count = 0;
    const bool ok = visit_config(c, section,
                                 [&](auto &x)
                                 {
                                     r.real(float(x));
                                     ++count;
                                 });
    r.data[2] = uint8_t(count);
    r.data[3] = uint8_t(count >> 8);
    return ok && r.size == 4 + 4 * count;
}
bool decode_interaction_section(const uint8_t *b, size_t size, InteractionConfig &c)
{
    if (!b || size < 4 || b[0] != 1 || read_u16(b + 2) != (size - 4) / 4 || (size - 4) % 4)
        return false;
    size_t offset = 4;
    bool ok = true;
    const bool section = visit_config(c, b[1],
                                      [&](auto &x)
                                      {
                                          if (offset + 4 > size)
                                          {
                                              ok = false;
                                              return;
                                          }
                                          const float v = read_float(b + offset);
                                          offset += 4;
                                          if (!std::isfinite(v))
                                          {
                                              ok = false;
                                              return;
                                          }
                                          if constexpr (std::is_same_v<std::decay_t<decltype(x)>, bool>)
                                          {
                                              if (v != 0 && v != 1)
                                              {
                                                  ok = false;
                                                  return;
                                              }
                                              x = v != 0;
                                          }
                                          else
                                              x = v;
                                      });
    return section && ok && offset == size;
}
size_t pack_interaction_config(InteractionConfig c, uint8_t *bytes, size_t capacity)
{
    if (!bytes || capacity < 4)
        return 0;
    size_t used = 4;
    bytes[0] = 1;
    bytes[1] = kInteractionSections;
    bytes[2] = bytes[3] = 0;
    for (unsigned i = 0; i < kInteractionSections; ++i)
    {
        MotorResponse r;
        if (!encode_interaction_section(c, uint8_t(i), r) || used + r.size > capacity)
            return 0;
        std::memcpy(bytes + used, r.data, r.size);
        used += r.size;
    }
    return used;
}
bool unpack_interaction_config(const uint8_t *b, size_t size, InteractionConfig &config)
{
    if (!b || size < 4 || b[0] != 1 || (b[1] != 10 && b[1] != kInteractionSections) || b[2] || b[3])
        return false;
    InteractionConfig candidate;
    size_t offset = 4;
    for (unsigned i = 0; i < b[1]; ++i)
    {
        if (offset + 4 > size || b[offset + 1] != i)
            return false;
        const size_t count = 4 + 4 * size_t(read_u16(b + offset + 2));
        if (count > size - offset || !decode_interaction_section(b + offset, count, candidate))
            return false;
        offset += count;
    }
    if (offset != size)
        return false;
    config = candidate;
    return true;
}
} // namespace app
