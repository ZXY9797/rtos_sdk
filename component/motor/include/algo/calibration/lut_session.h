#pragma once
#include <algo/calibration/angle_lut.h>

namespace algo::motor {

// Transport-independent owner/session and persistence state around LUT360 v2.
// A view borrowing staging must be detached before begin/read_begin overwrites
// it. A separately owned active table can remain attached during reads.
// Publish an output view only after readback verification.
class LutSession {
public:
    bool begin(const AngleLutRequest& request, uint16_t owner, uint32_t now_ms,
               bool control_disabled)
    {
        if (!control_disabled || request.action != AngleLutAction::Start || !owner ||
            !request.session || request.index || request.count != kAngleLutPointCount ||
            static_cast<unsigned>(request.table) > 1U) return false;
        if (transfer_.matches(request, owner)) {
            if (transfer_.read_only || transfer_.crc != request.crc) return false;
            active_ = !transfer_.committed;
            return true;
        }
        if (active_) return false;
        transfer_.begin(request, owner, now_ms);
        active_ = true;
        pending_ = false;
        return true;
    }

    bool accept(const AngleLutRequest& request, uint16_t owner, uint32_t now_ms)
    {
        if (!active_ || pending_ || transfer_.committed || transfer_.read_only ||
            request.action != AngleLutAction::Data || !transfer_.matches(request, owner) ||
            !transfer_.accept(request)) return false;
        transfer_.last_ms = now_ms;
        return true;
    }

    bool finish(const AngleLutRequest& request, uint16_t owner)
    {
        if (request.action != AngleLutAction::Finish || !transfer_.matches(request, owner) ||
            transfer_.read_only) return false;
        if (transfer_.committed) return true;
        if (!transfer_.complete()) return false;
        transfer_.data.valid = true;
        pending_ = true;
        return true;
    }

    bool complete_persistence(bool readback_verified)
    {
        if (!pending_) return false;
        pending_ = false;
        if (!readback_verified) {
            transfer_.data.valid = false;
            active_ = false;
            return false;
        }
        transfer_.committed = true;
        active_ = false;
        return true;
    }

    bool begin_read(const AngleLutRequest& request, uint16_t owner, uint32_t now_ms,
                    const MotorAngleLutData& stored, bool control_disabled)
    {
        if (!control_disabled || !stored.valid || !owner || !request.session ||
            request.action != AngleLutAction::ReadBegin || request.index || request.count ||
            static_cast<unsigned>(request.table) > 1U) return false;
        if (transfer_.matches(request, owner) && transfer_.read_only) return true;
        if (active_) return false;
        transfer_.begin(request, owner, now_ms);
        transfer_.data = stored;
        transfer_.read_only = true;
        transfer_.crc = transfer_.checksum();
        active_ = true;
        pending_ = false;
        return true;
    }

    const float* read(const AngleLutRequest& request, uint16_t owner, uint32_t now_ms)
    {
        if (!active_ || !transfer_.read_only || !transfer_.matches(request, owner) ||
            request.action != AngleLutAction::Read || !request.count ||
            request.count > kAngleLutChunkPoints || request.index >= kAngleLutPointCount ||
            request.count > kAngleLutPointCount - request.index) return nullptr;
        transfer_.last_ms = now_ms;
        return &transfer_.data.angle_offset_rad[request.index];
    }

    bool abort(const AngleLutRequest& request, uint16_t owner)
    {
        if (request.action != AngleLutAction::Abort || !transfer_.matches(request, owner))
            return false;
        active_ = pending_ = false;
        transfer_.session = 0U;
        return true;
    }

    bool expire(uint32_t now_ms)
    {
        if (!active_ || uint32_t(now_ms - transfer_.last_ms) <= 10000U) return false;
        active_ = pending_ = false;
        transfer_.session = 0U;
        return true;
    }

    const MotorAngleLutData& data() const { return transfer_.data; }
    bool active() const { return active_; }
    uint32_t checksum() const { return transfer_.crc; }
    bool pending_persistence() const { return pending_; }
    bool apply_output() const {
        return transfer_.committed && transfer_.table == AngleLutTable::Output;
    }

private:
    AngleLutTransfer transfer_ {};
    bool active_ {false};
    bool pending_ {false};
};

}  // namespace algo::motor
