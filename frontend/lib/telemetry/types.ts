/**
 * TypeScript mirror of the FSOC frontend data contract.
 * Authoritative source: docs/18_FRONTEND_DATA_CONTRACT.md  +  include/fsoc/demo.hpp
 *
 * The C++ core is radians / rad·s⁻¹ / m / m·s⁻¹ / px. This transport shape is the
 * documented UI boundary: angles in DEGREES (`*Deg` / `*DegS`), positions in
 * metres, pixels in pixels. Absent measurements are `null` (never -1 / NaN).
 *
 * The frontend consumes these fields as given. It must NEVER recompute PID,
 * tracking error, visibility, the beacon centroid, or camera dynamics.
 */

export type TrackingState = "TRACKING" | "TARGET_LOST";

/** application/session lifecycle — NOT a tracking state (docs/18) */
export type RunState = "READY" | "RUNNING" | "PAUSED" | "FINISHED";

export interface Vec3 {
  x: number;
  y: number;
  z: number;
}

export interface DemoSnapshot {
  simulationTime: number; // seconds
  frame: number; // frame index (0-based)
  trackingState: TrackingState;

  target: {
    position: Vec3; // metres, world frame (+X fwd / +Y right / +Z up)
    velocity: Vec3; // m/s
  };

  camera: {
    panDeg: number;
    tiltDeg: number;
    panRateDegS: number; // rate the actuator APPLIED (post clamp)
    tiltRateDegS: number;
    horizontalFovDeg: number;
    verticalFovDeg: number;
  };

  detection: {
    detected: boolean;
    xPx: number | null; // detected centroid, 640x480 sensor, origin TL
    yPx: number | null;
  };

  tracking: {
    errorXPx: number | null;
    errorYPx: number | null;
    panErrorDeg: number | null;
    tiltErrorDeg: number | null;
    totalErrorDeg: number | null; // hypot(pan, tilt)
  };

  control: {
    panCommandDegS: number; // PID output, PRE actuator clamp ({0,0} on loss / open-loop)
    tiltCommandDegS: number;
    panSaturated: boolean; // axis at the actuator rate limit
    tiltSaturated: boolean;
  };

  /** DIAGNOSTIC ONLY — detected centroid vs exact projection (not in the contract) */
  detectionErrorPx?: number | null;
  /** TRUTH visibility flag (target inside the FOV) — diagnostic */
  targetVisible?: boolean;

  /**
   * DIAGNOSTIC ONLY — Stage-3 AI + Safe Hybrid perception (ADR-018). Optional:
   * absent when reading an older fixture recorded before these CSV columns
   * existed. `mode` is "CLASSICAL" for every run that doesn't pass --mode to
   * fsoc_demo. Never used to recompute tracking/control — display only.
   */
  perception?: {
    mode: "CLASSICAL" | "AI" | "HYBRID";
    source: "NONE" | "CLASSICAL" | "AI" | "HYBRID_AGREEMENT";
    aiCandidateDetected: boolean;
    aiPresenceProbability: number | null;
    aiInferenceMs: number | null;
    classicalAiDistancePx: number | null;
    rejectionReason: "NOT_APPLICABLE" | "AI_ONLY_UNVERIFIED" | "DETECTOR_DISAGREEMENT";
  };

  /**
   * DIAGNOSTIC ONLY — P0-v2 state estimator (alpha-beta filter + temporal
   * gate, ADR-019). Optional: absent when reading a fixture recorded before
   * these CSV columns existed, OR when the run had tracker_enabled=false (the
   * default) — in that case lockState is the constant "SEARCHING" for every
   * frame, matching a pre-tracker run's signature. Never used to recompute
   * tracking/control — display only.
   */
  tracker?: {
    lockState: "SEARCHING" | "ACQUIRING" | "TRACKING" | "COASTING" | "LOST";
    xPx: number | null;
    yPx: number | null;
    vxPxS: number | null;
    vyPxS: number | null;
    confidence: number | null;
    isPrediction: boolean;
    coastFrames: number;
  };
}

export interface SimulationMeta {
  scenario: string;
  demoScenario: string;
  label: string;
  controlEnabled: boolean;
  durationS: number;
  frames: number;
  simRateHz: number;
  /** where these frames came from */
  source: "engine" | "replay";
  /** engine build path used (engine source only) */
  enginePath?: string;
  generatedAt?: string;
  /** deterministic Step-10 expected result for this scenario */
  expected: {
    detectionPct: number;
    rmsDeg: number;
    p95Deg: number;
    maxDeg: number;
    lostFrames: number;
  };
}

export interface SimulationPayload {
  meta: SimulationMeta;
  frames: DemoSnapshot[];
}

/** derived event log entry (from telemetry transitions — never random) */
export type SimEventKind =
  | "TRACK_ACQUIRED"
  | "TARGET_LOST"
  | "REACQUIRED"
  | "PAN_LIMIT"
  | "TILT_LIMIT"
  | "RATE_NOMINAL"
  | "RUN_START"
  | "RUN_END";

export interface SimEvent {
  frame: number;
  time: number;
  kind: SimEventKind;
  label: string;
  severity: "nominal" | "info" | "warning" | "critical";
}
