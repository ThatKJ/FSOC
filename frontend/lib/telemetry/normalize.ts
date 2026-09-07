/**
 * Normalizes the 27-column `fsoc_demo --csv` output (radians, C++ core units)
 * into the DemoSnapshot transport shape (degrees at the UI boundary).
 *
 * This is a UNIT + FIELD-NAME transform only — no physics. Column order and
 * semantics are fixed by `CsvTelemetryLogger::column_names()` in src/telemetry.cpp
 * and documented in docs/08_TELEMETRY_SCHEMA.md / docs/18_FRONTEND_DATA_CONTRACT.md.
 */

import type { DemoSnapshot, TrackingState } from "./types";

const RAD2DEG = 180 / Math.PI;

/** CSV columns, in order (must match src/telemetry.cpp CsvTelemetryLogger::column_names) */
export const CSV_COLUMNS = [
  "simulation_time_s",
  "frame_index",
  "target_visible",
  "target_detected",
  "target_position_x_m",
  "target_position_y_m",
  "target_position_z_m",
  "target_velocity_x_mps",
  "target_velocity_y_mps",
  "target_velocity_z_mps",
  "detected_x_px",
  "detected_y_px",
  "pixel_error_x_px",
  "pixel_error_y_px",
  "angular_error_pan_rad",
  "angular_error_tilt_rad",
  "angular_error_total_rad",
  "camera_pan_rad",
  "camera_tilt_rad",
  "command_pan_rate_rad_s",
  "command_tilt_rate_rad_s",
  "applied_pan_rate_rad_s",
  "applied_tilt_rate_rad_s",
  "pan_saturated",
  "tilt_saturated",
  "detection_error_px",
  "tracking_state",
  "perception_mode",
  "perception_source",
  "ai_candidate_detected",
  "ai_presence_probability",
  "ai_inference_ms",
  "classical_ai_distance_px",
  "perception_rejection_reason",
  "tracker_lock_state",
  "tracker_x_px",
  "tracker_y_px",
  "tracker_vx_px_s",
  "tracker_vy_px_s",
  "tracker_confidence",
  "tracker_is_prediction",
  "tracker_coast_frames",
] as const;

/** minimal CSV split — the FSOC telemetry CSV has no quoted / comma-bearing fields */
export function parseCsv(text: string): { header: string[]; rows: string[][] } {
  const lines = text.replace(/\r\n/g, "\n").trim().split("\n");
  if (lines.length === 0) return { header: [], rows: [] };
  const header = lines[0].split(",");
  const rows = lines.slice(1).map((l) => l.split(","));
  return { header, rows };
}

function num(v: string | undefined): number {
  if (v === undefined) return NaN;
  const t = v.trim();
  if (t === "") return NaN;
  return Number(t);
}

/** empty CSV cell -> null (the frozen "no sentinel" rule) */
function optNum(v: string | undefined): number | null {
  if (v === undefined) return null;
  const t = v.trim();
  if (t === "") return null;
  const n = Number(t);
  return Number.isFinite(n) ? n : null;
}

function optDeg(v: string | undefined): number | null {
  const n = optNum(v);
  return n === null ? null : n * RAD2DEG;
}

function bool01(v: string | undefined): boolean {
  return (v ?? "").trim() === "1";
}

export function rowToSnapshot(row: string[], header?: string[]): DemoSnapshot {
  // index by header when provided, else assume canonical order
  const idx = (name: string): number =>
    header ? header.indexOf(name) : CSV_COLUMNS.indexOf(name as (typeof CSV_COLUMNS)[number]);
  const get = (name: string): string | undefined => {
    const i = idx(name);
    return i >= 0 ? row[i] : undefined;
  };

  const stateRaw = (get("tracking_state") ?? "").trim();
  const trackingState: TrackingState =
    stateRaw === "TargetLost" || stateRaw === "TARGET_LOST" ? "TARGET_LOST" : "TRACKING";

  // Stage-3 perception columns are additive: absent entirely on older
  // fixtures recorded before they existed (idx returns -1 for all of them).
  type PerceptionInfo = NonNullable<DemoSnapshot["perception"]>;
  const hasPerceptionColumns = idx("perception_mode") >= 0;
  const perception: PerceptionInfo | undefined = hasPerceptionColumns
    ? {
        mode: (get("perception_mode")?.trim() || "CLASSICAL") as PerceptionInfo["mode"],
        source: (get("perception_source")?.trim() || "NONE") as PerceptionInfo["source"],
        aiCandidateDetected: bool01(get("ai_candidate_detected")),
        aiPresenceProbability: optNum(get("ai_presence_probability")),
        aiInferenceMs: optNum(get("ai_inference_ms")),
        classicalAiDistancePx: optNum(get("classical_ai_distance_px")),
        rejectionReason: (get("perception_rejection_reason")?.trim() ||
          "NOT_APPLICABLE") as PerceptionInfo["rejectionReason"],
      }
    : undefined;

  // P0-v2 tracker columns are additive: absent entirely on older fixtures
  // recorded before they existed (idx returns -1 for all of them).
  type TrackerInfo = NonNullable<DemoSnapshot["tracker"]>;
  const hasTrackerColumns = idx("tracker_lock_state") >= 0;
  const tracker: TrackerInfo | undefined = hasTrackerColumns
    ? {
        lockState: (get("tracker_lock_state")?.trim() || "SEARCHING") as TrackerInfo["lockState"],
        xPx: optNum(get("tracker_x_px")),
        yPx: optNum(get("tracker_y_px")),
        vxPxS: optNum(get("tracker_vx_px_s")),
        vyPxS: optNum(get("tracker_vy_px_s")),
        confidence: optNum(get("tracker_confidence")),
        isPrediction: bool01(get("tracker_is_prediction")),
        coastFrames: Math.round(num(get("tracker_coast_frames")) || 0),
      }
    : undefined;

  return {
    simulationTime: num(get("simulation_time_s")),
    frame: Math.round(num(get("frame_index"))),
    trackingState,
    target: {
      position: {
        x: num(get("target_position_x_m")),
        y: num(get("target_position_y_m")),
        z: num(get("target_position_z_m")),
      },
      velocity: {
        x: num(get("target_velocity_x_mps")),
        y: num(get("target_velocity_y_mps")),
        z: num(get("target_velocity_z_mps")),
      },
    },
    camera: {
      panDeg: num(get("camera_pan_rad")) * RAD2DEG,
      tiltDeg: num(get("camera_tilt_rad")) * RAD2DEG,
      panRateDegS: num(get("applied_pan_rate_rad_s")) * RAD2DEG,
      tiltRateDegS: num(get("applied_tilt_rate_rad_s")) * RAD2DEG,
      horizontalFovDeg: 20.0,
      verticalFovDeg: 15.0,
    },
    detection: {
      detected: bool01(get("target_detected")),
      xPx: optNum(get("detected_x_px")),
      yPx: optNum(get("detected_y_px")),
    },
    tracking: {
      errorXPx: optNum(get("pixel_error_x_px")),
      errorYPx: optNum(get("pixel_error_y_px")),
      panErrorDeg: optDeg(get("angular_error_pan_rad")),
      tiltErrorDeg: optDeg(get("angular_error_tilt_rad")),
      totalErrorDeg: optDeg(get("angular_error_total_rad")),
    },
    control: {
      panCommandDegS: num(get("command_pan_rate_rad_s")) * RAD2DEG,
      tiltCommandDegS: num(get("command_tilt_rate_rad_s")) * RAD2DEG,
      panSaturated: bool01(get("pan_saturated")),
      tiltSaturated: bool01(get("tilt_saturated")),
    },
    detectionErrorPx: optNum(get("detection_error_px")),
    perception,
    tracker,
    targetVisible: bool01(get("target_visible")),
  };
}

export function csvToSnapshots(text: string): DemoSnapshot[] {
  const { header, rows } = parseCsv(text);
  const hasCanonicalHeader = header[0] === "simulation_time_s";
  return rows
    .filter((r) => r.length >= 20)
    .map((r) => rowToSnapshot(r, hasCanonicalHeader ? header : undefined));
}
