/**
 * SERVER-ONLY. Local C++ engine integration — additive, non-invasive.
 *
 *   browser -> /api/simulation/[scenario] -> (this) -> ../build/debug/fsoc_demo
 *           -> real 27-col telemetry CSV -> normalized DemoSnapshot frames
 *
 * It invokes the EXISTING `fsoc_demo` binary. It never rebuilds, retunes, or
 * touches C++ source. If the binary is missing / fails, callers fall back to the
 * checked-in deterministic replay fixtures — the visual layer is identical.
 */

import { execFile } from "node:child_process";
import { mkdtemp, readFile, rm } from "node:fs/promises";
import { existsSync } from "node:fs";
import os from "node:os";
import path from "node:path";
import { promisify } from "node:util";

import { csvToSnapshots } from "@/lib/telemetry/normalize";
import type { DemoSnapshot } from "@/lib/telemetry/types";
import { SCENARIOS, type ScenarioId } from "@/lib/baseline/constants";

const execFileAsync = promisify(execFile);

/** repo root — `frontend/` lives directly under it */
function repoRoot(): string {
  return path.resolve(process.cwd(), "..");
}

export function engineBinaryPath(): string {
  const override = process.env.FSOC_DEMO_BIN;
  if (override) return path.isAbsolute(override) ? override : path.resolve(repoRoot(), override);
  return path.resolve(repoRoot(), "build", "debug", "fsoc_demo");
}

export function engineAvailable(): boolean {
  try {
    return existsSync(engineBinaryPath());
  } catch {
    return false;
  }
}

export interface EngineRunResult {
  frames: DemoSnapshot[];
  enginePath: string;
  durationOverrideS?: number;
}

/**
 * Runs `fsoc_demo <scenario> --csv <tmp> --quiet` and returns normalized frames.
 * Throws if the binary is missing or exits non-zero.
 */
export async function runEngineScenario(
  scenario: ScenarioId,
  opts: { durationS?: number; mode?: "classical" | "ai" | "hybrid"; trackerEnabled?: boolean } = {},
): Promise<EngineRunResult> {
  const bin = engineBinaryPath();
  if (!existsSync(bin)) {
    throw new Error(`fsoc_demo not found at ${bin} (build it: cmake --build --preset debug)`);
  }

  const meta = SCENARIOS[scenario];
  const dir = await mkdtemp(path.join(os.tmpdir(), "fsoc-demo-"));
  const csvPath = path.join(dir, `${scenario}.csv`);
  const args = [scenario, "--csv", csvPath, "--quiet"];
  if (opts.durationS && Number.isFinite(opts.durationS) && opts.durationS > 0) {
    args.push("--duration", String(opts.durationS));
  }
  if (opts.mode && opts.mode !== "classical") {
    args.push("--mode", opts.mode);
  }
  if (opts.trackerEnabled) {
    args.push("--tracker");
  }

  try {
    // cwd MUST be the repo root, not the tmp dir: fsoc_demo resolves its AI
    // model path ("models/tiny_beacon_net.onnx") relative to its CWD, with no
    // --model override. --csv already gets an absolute path, so this has no
    // effect on where the CSV lands.
    await execFileAsync(bin, args, {
      cwd: repoRoot(),
      timeout: 30_000,
      maxBuffer: 8 * 1024 * 1024,
    });
    const csv = await readFile(csvPath, "utf8");
    const frames = csvToSnapshots(csv);
    if (frames.length === 0) throw new Error("engine produced 0 telemetry rows");
    // sanity: expected frame count for the default duration
    if (!opts.durationS && Math.abs(frames.length - meta.frames) > 2) {
      // non-fatal — engine is authoritative — but surface it in logs
      console.warn(
        `[fsoc engine] ${scenario}: got ${frames.length} frames, expected ~${meta.frames}`,
      );
    }
    return { frames, enginePath: bin, durationOverrideS: opts.durationS };
  } finally {
    await rm(dir, { recursive: true, force: true }).catch(() => {});
  }
}
