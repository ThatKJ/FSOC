#pragma once

#include <optional>

#include "fsoc/config.hpp"

namespace fsoc {

// ---------------------------------------------------------------------------
// VirtualPanTiltActuator — HONEST bookkeeping, not a physical actuator
// ---------------------------------------------------------------------------
//
// Consumes the same ControlCommand-shaped angular RATES the simulation's
// PanTiltCamera::step() consumes, and integrates them into a bookkeeping
// pan/tilt angle with rate saturation — but it does not move anything and it
// does NOT feed back into how the next real-camera frame's pixel error is
// interpreted (the phone's actual physical orientation is controlled by a
// human, not by this class). Telemetry consumers must label this
// ACTUATOR_TYPE = VIRTUAL and never claim it moved a physical gimbal.
//
// Deliberately a separate, minimal class rather than reusing PanTiltCamera:
// PanTiltCamera is a world-frame simulation camera model (position, FOV,
// projection) — reusing it here would blur exactly the SIMULATION vs
// VIRTUAL_ACTUATOR boundary this milestone exists to keep sharp.

struct VirtualActuatorConfig {
    double max_pan_rate_rad_s{deg_to_rad(30.0)};
    double max_tilt_rate_rad_s{deg_to_rad(30.0)};

    // Optional soft travel bounds on the integrated bookkeeping angle (NOT a
    // real gimbal limit — purely to keep the running total sane over a long
    // session). std::nullopt on either side means unbounded on that side.
    std::optional<double> min_tilt_rad{};
    std::optional<double> max_tilt_rad{};

    // max_pan_rate_rad_s/max_tilt_rate_rad_s finite > 0; if both tilt bounds
    // are set, min_tilt_rad < max_tilt_rad. Throws std::invalid_argument otherwise.
    void validate() const;
};

struct VirtualActuatorState {
    double pan_rad{0.0};   // integrated VIRTUAL pan angle — bookkeeping only
    double tilt_rad{0.0};  // integrated VIRTUAL tilt angle — bookkeeping only
    double pan_rate_rad_s{0.0};   // rate actually applied this step (post-saturation)
    double tilt_rate_rad_s{0.0};
    bool pan_saturated{false};
    bool tilt_saturated{false};
};

class VirtualPanTiltActuator {
public:
    explicit VirtualPanTiltActuator(VirtualActuatorConfig config = {});

    [[nodiscard]] const VirtualActuatorConfig& config() const noexcept { return config_; }

    // Saturates the commanded rates to the configured limits, integrates over
    // dt_s, and (if configured) clamps the resulting tilt to [min_tilt_rad,
    // max_tilt_rad]. Throws std::invalid_argument if dt_s is not finite and
    // > 0, or if either commanded rate is not finite — state is NOT mutated
    // on throw, mirroring PanTiltCamera::step()'s contract.
    [[nodiscard]] VirtualActuatorState step(
        double pan_rate_cmd_rad_s,
        double tilt_rate_cmd_rad_s,
        double dt_s);

    // Back to zero angle, zero rate, no saturation.
    void reset() noexcept;

    [[nodiscard]] const VirtualActuatorState& state() const noexcept { return state_; }

private:
    VirtualActuatorConfig config_;
    VirtualActuatorState state_{};
};

}  // namespace fsoc
