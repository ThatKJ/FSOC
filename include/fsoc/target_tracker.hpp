#pragma once

#include <cstddef>
#include <optional>

#include "fsoc/image_geometry.hpp"

namespace fsoc {

// ---------------------------------------------------------------------------
// TargetTracker (P0 v2) — minimal, explainable state estimation + short-
// horizon prediction + temporal-consistency gating over the PIXEL centroid
// the perception layer already accepted.
// ---------------------------------------------------------------------------
//
// This is NOT a Kalman/UKF filter (deliberately -- see DECISIONS.md history:
// the project repeatedly and explicitly deferred that complexity). It is a
// classic alpha-beta (g-h) filter: a constant-velocity position/velocity
// estimate corrected by each new measurement, with an explicit, hard-bounded
// coast (short-horizon prediction) for brief detection gaps, and an explicit
// implausible-jump gate.
//
// Contract:
//   - operates ONLY on the control-facing centroid the perception layer
//     (resolve_perception / Safe Hybrid) already produced -- it never sees
//     TargetState, truth, or raw un-fused detector candidates;
//   - `update()` is a pure state transition: same (measurement, dt) sequence
//     from reset() always produces the same TrackedState sequence;
//   - never extrapolates indefinitely: `coast_frames` is hard-capped by
//     `max_coast_frames` (protocol §"empirical justification" below), past
//     which the track is unconditionally declared Lost;
//   - the outlier gate is a TEMPORAL CONSISTENCY check only (current
//     candidate vs. this tracker's OWN prior prediction) -- it never reads
//     world truth, matching the ADR-018 / docs/19 §9 ground-truth boundary
//     for a future motion-consistency gate.
//
// Empirical parameter justification (not arbitrary):
//   max_coast_frames = 2 (40 ms @ 50 Hz) and outlier_gate_px = 60 px are
//   grounded in the existing Step-10 sinusoidal-tracking evidence: the
//   fastest validated scenario (+/-12.4 deg swing) has a measured MAXIMUM
//   frame-to-frame detected-centroid delta of 6.07 px (p99 0.49 px, mean
//   0.27 px, generated/step10/sinusoidal.csv). 60 px is a ~10x margin over
//   that worst case -- comfortably separating real tracking dynamics /
//   detector sub-pixel noise from the 300-700 px catastrophic wrong-blob
//   jumps Stage-4 measured under clutter (docs/21, docs/MVP_METRICS.md §2).
//   See docs/MVP_ABLATION.md for the evaluation that re-validates this.

enum class LockState {
    Searching,  // no track; no accepted measurement yet (or track fully lost)
    Acquiring,  // a candidate has appeared but hasn't been confirmed for
                // acquire_frames_required consecutive frames yet
    Tracking,   // confirmed track, updated by a real measurement this frame
    Coasting,   // confirmed track, no measurement this frame (or one was
                // gated out as an implausible jump) -- short-horizon predicted
    Lost,       // coast horizon exceeded; track dropped, one frame only
                // (the next update() call returns Searching)
};

[[nodiscard]] const char* to_string(LockState state) noexcept;

struct TargetTrackerConfig {
    // Alpha-beta (g-h) filter gains: position and velocity correction
    // fractions applied to the innovation (measurement - prediction). Both in
    // (0, 1]. Higher = trusts new measurements more / smooths less.
    double alpha{0.6};
    double beta{0.3};

    // Consecutive, MUTUALLY CONSISTENT real measurements required before
    // Acquiring -> Tracking. "Consistent" means each new measurement during
    // Acquiring is itself checked against outlier_gate_px (same gate
    // established tracks use): a measurement that lands far from the
    // acquisition-in-progress restarts acquisition fresh at that new
    // measurement rather than counting toward confirmation. Without this, 3
    // consecutive detections at 3 spatially incoherent positions (e.g.
    // Classical's brightest-blob rule false-locking onto a different random
    // clutter blob every frame) would confirm a garbage track just as readily
    // as 3 consistent ones -- see docs/MVP_ABLATION.md Phase E. An unconfirmed
    // candidate that disappears before this many frames drops straight back
    // to Searching (never coasts an unconfirmed track).
    int acquire_frames_required{3};

    // Hard coast horizon (consecutive frames without an accepted real
    // measurement the tracker may bridge by prediction alone) -- see the
    // empirical justification above. 0 disables coasting entirely.
    int max_coast_frames{2};

    // Confidence multiplier applied once per coasted frame (compounding
    // decay toward 0 the longer a track survives on prediction alone).
    double confidence_decay_per_coast_frame{0.5};

    // Temporal-consistency gate, in pixels: once Tracking or Coasting, a new
    // measurement farther than this from THIS TRACKER'S OWN predicted
    // position is treated as an implausible jump -- gated out (handled as if
    // no measurement arrived this frame) rather than accepted. Never
    // evaluated against Acquiring (an unconfirmed track has no trustworthy
    // prediction yet).
    double outlier_gate_px{60.0};

    // alpha, beta in (0,1]; acquire_frames_required >= 1; max_coast_frames >= 0;
    // confidence_decay_per_coast_frame in [0,1]; outlier_gate_px finite > 0.
    // Throws std::invalid_argument otherwise.
    void validate() const;
};

struct TrackedState {
    LockState lock_state{LockState::Searching};

    double x_px{};
    double y_px{};
    double vx_px_s{};
    double vy_px_s{};

    double confidence{0.0};        // in [0,1]; 1.0 on a fresh accepted measurement
    std::size_t age_frames{0};     // consecutive frames since acquisition (Tracking+Coasting)
    std::size_t coast_frames{0};   // consecutive frames currently coasting (0 when Tracking)

    bool is_prediction{false};     // true iff (x_px,y_px) this frame came from
                                    // prediction, not a real accepted measurement
    bool measurement_rejected_outlier{false};  // diagnostic: a measurement arrived
                                                // this frame but failed the gate
};

// Explicit, visible control-safety policy (kept OUTSIDE TargetTracker itself,
// which stays a pure state estimator with no notion of "is this safe to
// steer with"): a Tracking state is always safe; a Coasting (predicted)
// state is safe only while its decayed confidence has not yet dropped below
// `min_confidence_to_steer`. Searching/Acquiring/Lost are never safe (no
// confirmed track to predict from). This is the ONE rule the controller
// actually uses to decide whether to steer toward a predicted position
// during a brief dropout -- see docs/MVP_ABLATION.md for the evaluation
// that justifies the default threshold.
[[nodiscard]] bool is_safe_to_steer(const TrackedState& state, double min_confidence_to_steer) noexcept;

class TargetTracker {
public:
    explicit TargetTracker(TargetTrackerConfig config = {});

    [[nodiscard]] const TargetTrackerConfig& config() const noexcept { return config_; }

    // Advance exactly one step. `measurement` is the perception layer's
    // accepted control-facing centroid this frame (std::nullopt if none).
    // Throws std::invalid_argument if dt_s is not finite and > 0 -- state is
    // NOT mutated on throw. Deterministic; no heap allocation.
    [[nodiscard]] TrackedState update(std::optional<ImagePoint> measurement, double dt_s);

    // Back to LockState::Searching with a zeroed state, as if newly constructed.
    void reset() noexcept;

    [[nodiscard]] const TrackedState& state() const noexcept { return state_; }

private:
    void coast_or_lose(double predicted_x_px, double predicted_y_px, bool measurement_rejected);
    void begin_acquisition(const ImagePoint& measurement);

    TargetTrackerConfig config_;
    TrackedState state_{};
    bool have_prior_{false};
    std::size_t consecutive_measurements_{0};
};

}  // namespace fsoc
