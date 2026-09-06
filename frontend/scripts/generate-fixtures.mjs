#!/usr/bin/env node
/**
 * Pre-generate deterministic replay fixtures from the REAL C++ engine.
 *
 *   frontend/ $  node scripts/generate-fixtures.mjs
 *
 * Runs `../build/debug/fsoc_demo <scenario> --csv <tmp> --quiet` for each of the
 * five DemoScenario presets, normalizes the 27-column telemetry CSV (radians ->
 * degrees, empty cell -> null) into DemoSnapshot frames, and writes:
 *
 *   lib/telemetry/fixtures/<scenario>.json   (meta + all frames)
 *   lib/telemetry/fixtures/manifest.json     (index + validated metrics)
 *
 * These fixtures are checked in so the app is deployable without a C++ toolchain.
 * They are byte-identical to the engine output. No simulation math lives here.
 */

import { execFile } from "node:child_process";
import { mkdtemp, mkdir, readFile, writeFile, rm } from "node:fs/promises";
import { existsSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const __dirname = path.dirname(fileURLToPath(import.meta.url));
const frontendRoot = path.resolve(__dirname, "..");
const repoRoot = path.resolve(frontendRoot, "..");

const RAD2DEG = 180 / Math.PI;

const SCENARIOS = {
  static: { demoScenario: "StaticAcquisition", label: "Static Acquisition", controlEnabled: true, durationS: 4, frames: 200, expected: { detectionPct: 100.0, rmsDeg: 0.4691, p95Deg: 0.2885, maxDeg: 4.1275, lostFrames: 0 } },
  sinusoidal: { demoScenario: "SinusoidalTracking", label: "Sinusoidal Tracking", controlEnabled: true, durationS: 20, frames: 1000, expected: { detectionPct: 100.0, rmsDeg: 0.5461, p95Deg: 0.7899, maxDeg: 0.7982, lostFrames: 0 } },
  loss: { demoScenario: "LossReacquisition", label: "Target Loss & Re-entry", controlEnabled: true, durationS: 8, frames: 400, expected: { detectionPct: 73.2, rmsDeg: 4.9328, p95Deg: 8.8284, maxDeg: 9.9692, lostFrames: 107 } },
  open: { demoScenario: "OpenLoop", label: "Open Loop", controlEnabled: false, durationS: 20, frames: 1000, expected: { detectionPct: 57.4, rmsDeg: 6.4549, p95Deg: 9.8112, maxDeg: 10.1916, lostFrames: 426 } },
  closed: { demoScenario: "ClosedLoop", label: "Closed Loop", controlEnabled: true, durationS: 20, frames: 1000, expected: { detectionPct: 100.0, rmsDeg: 0.5461, p95Deg: 0.7899, maxDeg: 0.7982, lostFrames: 0 } },
};

function binPath() {
  const override = process.env.FSOC_DEMO_BIN;
  if (override) return path.isAbsolute(override) ? override : path.resolve(repoRoot, override);
  return path.resolve(repoRoot, "build", "debug", "fsoc_demo");
}

function optNum(v) {
  if (v === undefined) return null;
  const t = String(v).trim();
  if (t === "") return null;
  const n = Number(t);
  return Number.isFinite(n) ? n : null;
}
function num(v) {
  const n = optNum(v);
  return n === null ? NaN : n;
}
function optDeg(v) {
  const n = optNum(v);
  return n === null ? null : n * RAD2DEG;
}
function bool01(v) {
  return String(v ?? "").trim() === "1";
}

function csvToSnapshots(text) {
  const lines = text.replace(/\r\n/g, "\n").trim().split("\n");
  const header = lines[0].split(",");
  const col = (name) => header.indexOf(name);
  const c = {
    t: col("simulation_time_s"), fi: col("frame_index"),
    vis: col("target_visible"), det: col("target_detected"),
    px: col("target_position_x_m"), py: col("target_position_y_m"), pz: col("target_position_z_m"),
    vx: col("target_velocity_x_mps"), vy: col("target_velocity_y_mps"), vz: col("target_velocity_z_mps"),
    dx: col("detected_x_px"), dy: col("detected_y_px"),
    ex: col("pixel_error_x_px"), ey: col("pixel_error_y_px"),
    ap: col("angular_error_pan_rad"), at: col("angular_error_tilt_rad"), atot: col("angular_error_total_rad"),
    cpan: col("camera_pan_rad"), ctilt: col("camera_tilt_rad"),
    cmdp: col("command_pan_rate_rad_s"), cmdt: col("command_tilt_rate_rad_s"),
    apr: col("applied_pan_rate_rad_s"), atr: col("applied_tilt_rate_rad_s"),
    psat: col("pan_saturated"), tsat: col("tilt_saturated"),
    derr: col("detection_error_px"), st: col("tracking_state"),
    pmode: col("perception_mode"), psrc: col("perception_source"),
    aicand: col("ai_candidate_detected"), aiprob: col("ai_presence_probability"),
    aims: col("ai_inference_ms"), caidist: col("classical_ai_distance_px"),
    prej: col("perception_rejection_reason"),
  };
  const hasPerceptionColumns = c.pmode >= 0;
  const rows = lines.slice(1).filter((l) => l.length > 0).map((l) => l.split(","));
  return rows.map((r) => {
    const stateRaw = String(r[c.st] ?? "").trim();
    return {
      simulationTime: num(r[c.t]),
      frame: Math.round(num(r[c.fi])),
      trackingState: stateRaw === "TargetLost" ? "TARGET_LOST" : "TRACKING",
      target: {
        position: { x: num(r[c.px]), y: num(r[c.py]), z: num(r[c.pz]) },
        velocity: { x: num(r[c.vx]), y: num(r[c.vy]), z: num(r[c.vz]) },
      },
      camera: {
        panDeg: num(r[c.cpan]) * RAD2DEG,
        tiltDeg: num(r[c.ctilt]) * RAD2DEG,
        panRateDegS: num(r[c.apr]) * RAD2DEG,
        tiltRateDegS: num(r[c.atr]) * RAD2DEG,
        horizontalFovDeg: 20.0,
        verticalFovDeg: 15.0,
      },
      detection: { detected: bool01(r[c.det]), xPx: optNum(r[c.dx]), yPx: optNum(r[c.dy]) },
      tracking: {
        errorXPx: optNum(r[c.ex]),
        errorYPx: optNum(r[c.ey]),
        panErrorDeg: optDeg(r[c.ap]),
        tiltErrorDeg: optDeg(r[c.at]),
        totalErrorDeg: optDeg(r[c.atot]),
      },
      control: {
        panCommandDegS: num(r[c.cmdp]) * RAD2DEG,
        tiltCommandDegS: num(r[c.cmdt]) * RAD2DEG,
        panSaturated: bool01(r[c.psat]),
        tiltSaturated: bool01(r[c.tsat]),
      },
      detectionErrorPx: optNum(r[c.derr]),
      targetVisible: bool01(r[c.vis]),
      perception: hasPerceptionColumns
        ? {
            mode: String(r[c.pmode] ?? "CLASSICAL").trim() || "CLASSICAL",
            source: String(r[c.psrc] ?? "NONE").trim() || "NONE",
            aiCandidateDetected: bool01(r[c.aicand]),
            aiPresenceProbability: optNum(r[c.aiprob]),
            aiInferenceMs: optNum(r[c.aims]),
            classicalAiDistancePx: optNum(r[c.caidist]),
            rejectionReason: String(r[c.prej] ?? "NOT_APPLICABLE").trim() || "NOT_APPLICABLE",
          }
        : undefined,
    };
  });
}

async function main() {
  const bin = binPath();
  if (!existsSync(bin)) {
    console.error(`\n  fsoc_demo not found at:\n    ${bin}\n`);
    console.error("  Build the C++ engine first, from the repo root:");
    console.error("    cmake --preset debug && cmake --build --preset debug\n");
    console.error("  (or set FSOC_DEMO_BIN to an absolute path)\n");
    process.exit(1);
  }

  const outDir = path.join(frontendRoot, "lib", "telemetry", "fixtures");
  await mkdir(outDir, { recursive: true });

  // relative label only — never commit an absolute home-dir path into fixtures
  const engineLabel = "build/debug/fsoc_demo";
  const manifest = { generatedAt: new Date().toISOString(), enginePath: engineLabel, scenarios: {} };

  for (const [id, cfg] of Object.entries(SCENARIOS)) {
    const dir = await mkdtemp(path.join(os.tmpdir(), "fsoc-fix-"));
    const csvPath = path.join(dir, `${id}.csv`);
    try {
      process.stdout.write(`  ${id.padEnd(11)} `);
      await execFileAsync(bin, [id, "--csv", csvPath, "--quiet"], { cwd: dir, timeout: 30_000, maxBuffer: 8 * 1024 * 1024 });
      const csv = await readFile(csvPath, "utf8");
      const frames = csvToSnapshots(csv);
      const lost = frames.filter((f) => f.trackingState === "TARGET_LOST").length;
      const detected = frames.filter((f) => f.detection.detected).length;
      const payload = {
        meta: {
          scenario: id,
          demoScenario: cfg.demoScenario,
          label: cfg.label,
          controlEnabled: cfg.controlEnabled,
          durationS: cfg.durationS,
          frames: frames.length,
          simRateHz: 50,
          source: "replay",
          enginePath: engineLabel,
          generatedAt: manifest.generatedAt,
          expected: cfg.expected,
        },
        frames,
      };
      await writeFile(path.join(outDir, `${id}.json`), JSON.stringify(payload));
      manifest.scenarios[id] = {
        frames: frames.length,
        detectedFrames: detected,
        lostFrames: lost,
        detectionPct: Number(((100 * detected) / frames.length).toFixed(2)),
        expected: cfg.expected,
      };
      console.log(`ok  ${frames.length} frames, ${lost} lost, ${((100 * detected) / frames.length).toFixed(1)}% detected`);
    } finally {
      await rm(dir, { recursive: true, force: true }).catch(() => {});
    }
  }

  await writeFile(path.join(outDir, "manifest.json"), JSON.stringify(manifest, null, 2));
  console.log(`\n  wrote ${Object.keys(SCENARIOS).length} fixtures + manifest.json to lib/telemetry/fixtures/\n`);
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});
