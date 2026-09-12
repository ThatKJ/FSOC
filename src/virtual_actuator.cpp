#include "fsoc/virtual_actuator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace fsoc {

void VirtualActuatorConfig::validate() const {
    if (!std::isfinite(max_pan_rate_rad_s) || max_pan_rate_rad_s <= 0.0) {
        throw std::invalid_argument("VirtualActuatorConfig: max_pan_rate_rad_s must be finite > 0.");
    }
    if (!std::isfinite(max_tilt_rate_rad_s) || max_tilt_rate_rad_s <= 0.0) {
        throw std::invalid_argument("VirtualActuatorConfig: max_tilt_rate_rad_s must be finite > 0.");
    }
    if (min_tilt_rad.has_value() && max_tilt_rad.has_value()) {
        if (!std::isfinite(*min_tilt_rad) || !std::isfinite(*max_tilt_rad) ||
            !(*min_tilt_rad < *max_tilt_rad)) {
            throw std::invalid_argument(
                "VirtualActuatorConfig: min_tilt_rad must be finite and < max_tilt_rad.");
        }
    }
}

VirtualPanTiltActuator::VirtualPanTiltActuator(VirtualActuatorConfig config) : config_(std::move(config)) {
    config_.validate();
}

VirtualActuatorState VirtualPanTiltActuator::step(
    double pan_rate_cmd_rad_s, double tilt_rate_cmd_rad_s, double dt_s) {
    if (!std::isfinite(dt_s) || dt_s <= 0.0) {
        throw std::invalid_argument("VirtualPanTiltActuator::step: dt_s must be finite and > 0.");
    }
    if (!std::isfinite(pan_rate_cmd_rad_s) || !std::isfinite(tilt_rate_cmd_rad_s)) {
        throw std::invalid_argument("VirtualPanTiltActuator::step: commanded rates must be finite.");
    }

    const bool pan_saturated = std::abs(pan_rate_cmd_rad_s) > config_.max_pan_rate_rad_s;
    const bool tilt_saturated = std::abs(tilt_rate_cmd_rad_s) > config_.max_tilt_rate_rad_s;
    const double pan_rate =
        std::clamp(pan_rate_cmd_rad_s, -config_.max_pan_rate_rad_s, config_.max_pan_rate_rad_s);
    double tilt_rate =
        std::clamp(tilt_rate_cmd_rad_s, -config_.max_tilt_rate_rad_s, config_.max_tilt_rate_rad_s);

    double pan_rad = state_.pan_rad + pan_rate * dt_s;
    double tilt_rad = state_.tilt_rad + tilt_rate * dt_s;

    if (config_.min_tilt_rad.has_value() && config_.max_tilt_rad.has_value()) {
        const double clamped = std::clamp(tilt_rad, *config_.min_tilt_rad, *config_.max_tilt_rad);
        if (clamped != tilt_rad) {
            tilt_rate = 0.0;  // travel limit reached — the integrated angle stops advancing
        }
        tilt_rad = clamped;
    }

    state_ = VirtualActuatorState{
        .pan_rad = pan_rad,
        .tilt_rad = tilt_rad,
        .pan_rate_rad_s = pan_rate,
        .tilt_rate_rad_s = tilt_rate,
        .pan_saturated = pan_saturated,
        .tilt_saturated = tilt_saturated,
    };
    return state_;
}

void VirtualPanTiltActuator::reset() noexcept {
    state_ = VirtualActuatorState{};
}

}  // namespace fsoc
