#include "fsoc/target_tracker.hpp"

#include <cmath>
#include <stdexcept>

namespace fsoc {

const char* to_string(const LockState state) noexcept {
    switch (state) {
        case LockState::Searching: return "SEARCHING";
        case LockState::Acquiring: return "ACQUIRING";
        case LockState::Tracking: return "TRACKING";
        case LockState::Coasting: return "COASTING";
        case LockState::Lost: return "LOST";
    }
    return "SEARCHING";
}

bool is_safe_to_steer(const TrackedState& state, const double min_confidence_to_steer) noexcept {
    if (state.lock_state == LockState::Tracking) {
        return true;
    }
    if (state.lock_state == LockState::Coasting) {
        return state.confidence >= min_confidence_to_steer;
    }
    return false;
}

void TargetTrackerConfig::validate() const {
    if (!(std::isfinite(alpha) && alpha > 0.0 && alpha <= 1.0)) {
        throw std::invalid_argument("TargetTrackerConfig: alpha must be finite in (0, 1].");
    }
    if (!(std::isfinite(beta) && beta > 0.0 && beta <= 1.0)) {
        throw std::invalid_argument("TargetTrackerConfig: beta must be finite in (0, 1].");
    }
    if (acquire_frames_required < 1) {
        throw std::invalid_argument("TargetTrackerConfig: acquire_frames_required must be >= 1.");
    }
    if (max_coast_frames < 0) {
        throw std::invalid_argument("TargetTrackerConfig: max_coast_frames must be >= 0.");
    }
    if (!(std::isfinite(confidence_decay_per_coast_frame) && confidence_decay_per_coast_frame >= 0.0 &&
          confidence_decay_per_coast_frame <= 1.0)) {
        throw std::invalid_argument(
            "TargetTrackerConfig: confidence_decay_per_coast_frame must be finite in [0, 1].");
    }
    if (!(std::isfinite(outlier_gate_px) && outlier_gate_px > 0.0)) {
        throw std::invalid_argument("TargetTrackerConfig: outlier_gate_px must be finite and > 0.");
    }
}

TargetTracker::TargetTracker(TargetTrackerConfig config) : config_(std::move(config)) {
    config_.validate();
}

void TargetTracker::reset() noexcept {
    state_ = TrackedState{};
    have_prior_ = false;
    consecutive_measurements_ = 0;
}

void TargetTracker::begin_acquisition(const ImagePoint& measurement) {
    // Velocity is reset to 0 -- a stale velocity from a previous, unrelated
    // (or just-discarded) track must never poison a new one.
    state_.x_px = measurement.x_px;
    state_.y_px = measurement.y_px;
    state_.vx_px_s = 0.0;
    state_.vy_px_s = 0.0;
    state_.confidence = 1.0;
    state_.age_frames = 1;
    state_.coast_frames = 0;
    state_.is_prediction = false;
    state_.measurement_rejected_outlier = false;
    consecutive_measurements_ = 1;
    state_.lock_state =
        (consecutive_measurements_ >= static_cast<std::size_t>(config_.acquire_frames_required))
            ? LockState::Tracking
            : LockState::Acquiring;
    have_prior_ = true;
}

void TargetTracker::coast_or_lose(
    const double predicted_x_px, const double predicted_y_px, const bool measurement_rejected) {
    if (static_cast<int>(state_.coast_frames) >= config_.max_coast_frames) {
        state_ = TrackedState{};
        state_.lock_state = LockState::Lost;
        state_.measurement_rejected_outlier = measurement_rejected;
        have_prior_ = false;  // fully drop the track -- the next measurement is a fresh acquisition
        return;
    }
    state_.x_px = predicted_x_px;
    state_.y_px = predicted_y_px;
    // velocity held constant through the coast (constant-velocity prediction)
    state_.confidence *= config_.confidence_decay_per_coast_frame;
    ++state_.coast_frames;
    ++state_.age_frames;
    state_.is_prediction = true;
    state_.measurement_rejected_outlier = measurement_rejected;
    state_.lock_state = LockState::Coasting;
}

TrackedState TargetTracker::update(const std::optional<ImagePoint> measurement, const double dt_s) {
    if (!std::isfinite(dt_s) || dt_s <= 0.0) {
        throw std::invalid_argument("TargetTracker::update: dt_s must be finite and > 0.");
    }

    const bool track_active =
        have_prior_ && (state_.lock_state == LockState::Tracking ||
                        state_.lock_state == LockState::Coasting ||
                        state_.lock_state == LockState::Acquiring);

    double predicted_x = state_.x_px;
    double predicted_y = state_.y_px;
    if (track_active) {
        predicted_x += state_.vx_px_s * dt_s;
        predicted_y += state_.vy_px_s * dt_s;
    }

    if (measurement.has_value()) {
        if (!track_active) {
            // Fresh acquisition: from Searching/Lost, or the very first call.
            begin_acquisition(*measurement);
            return state_;
        }

        const bool established =
            (state_.lock_state == LockState::Tracking || state_.lock_state == LockState::Coasting);
        const double jump_px = std::hypot(measurement->x_px - predicted_x, measurement->y_px - predicted_y);
        if (established && jump_px > config_.outlier_gate_px) {
            // Temporal-consistency gate: implausible jump from THIS TRACKER's
            // own prediction -- treat as no measurement this frame.
            coast_or_lose(predicted_x, predicted_y, /*measurement_rejected=*/true);
            return state_;
        }

        if (!established && jump_px > config_.outlier_gate_px) {
            // Acquisition-consistency gate: this candidate is spatially
            // incoherent with the acquisition already in progress (e.g.
            // Classical false-locking onto a different random clutter blob
            // each frame). Confirming a track on 3 consecutive but mutually
            // inconsistent positions would be worse than not tracking at all
            // -- restart acquisition fresh here rather than let it count
            // toward confirmation (docs/MVP_ABLATION.md Phase E).
            begin_acquisition(*measurement);
            return state_;
        }

        const double dx = measurement->x_px - predicted_x;
        const double dy = measurement->y_px - predicted_y;
        state_.x_px = predicted_x + config_.alpha * dx;
        state_.y_px = predicted_y + config_.alpha * dy;
        state_.vx_px_s += (config_.beta * dx) / dt_s;
        state_.vy_px_s += (config_.beta * dy) / dt_s;
        state_.confidence = 1.0;
        state_.coast_frames = 0;
        state_.is_prediction = false;
        state_.measurement_rejected_outlier = false;
        ++state_.age_frames;
        ++consecutive_measurements_;
        state_.lock_state = (state_.lock_state == LockState::Acquiring &&
                              consecutive_measurements_ < static_cast<std::size_t>(config_.acquire_frames_required))
                                 ? LockState::Acquiring
                                 : LockState::Tracking;
        return state_;
    }

    // No measurement this frame.
    consecutive_measurements_ = 0;
    if (!track_active) {
        state_ = TrackedState{};
        state_.lock_state = LockState::Searching;
        have_prior_ = false;
        return state_;
    }
    if (state_.lock_state == LockState::Acquiring) {
        // Unconfirmed candidate disappeared -- back to Searching, never coasts
        // a track that was never confirmed.
        state_ = TrackedState{};
        state_.lock_state = LockState::Searching;
        have_prior_ = false;
        return state_;
    }
    coast_or_lose(predicted_x, predicted_y, /*measurement_rejected=*/false);
    return state_;
}

}  // namespace fsoc
