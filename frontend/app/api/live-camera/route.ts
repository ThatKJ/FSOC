import { NextResponse } from "next/server";
import { readFile } from "node:fs/promises";
import { existsSync, statSync } from "node:fs";
import path from "node:path";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

/**
 * SERVER-ONLY. Mobile Phone Camera-in-the-Loop milestone.
 *
 *   fsoc_live (long-running C++ process, real camera) -> generated/live/telemetry.json
 *           -> (this route, polled) -> browser
 *
 * fsoc_live is a long-running process (a real camera session has no natural
 * "end of run" the way a deterministic scenario does), so it cannot be
 * invoked per-request the way /api/simulation/:scenario invokes fsoc_demo.
 * Instead it overwrites one JSON file after every processed frame; this
 * route is a plain snapshot read of whatever is currently on disk. This is
 * an honest polling read, not a live stream -- the frontend must poll it
 * (see docs/PHONE_CAMERA_METRICS.md "Live telemetry JSON schema" and
 * "Mission Control transport").
 *
 * GET /api/live-camera
 *   200  the most recent frame fsoc_live wrote, plus staleness info
 *   503  no live session is running / no telemetry has been written yet
 */

function repoRoot(): string {
  return path.resolve(process.cwd(), "..");
}

function telemetryPath(): string {
  const override = process.env.FSOC_LIVE_OUT;
  const dir = override ? (path.isAbsolute(override) ? override : path.resolve(repoRoot(), override))
                        : path.resolve(repoRoot(), "generated", "live");
  return path.join(dir, "telemetry.json");
}

export async function GET() {
  const file = telemetryPath();
  if (!existsSync(file)) {
    return NextResponse.json(
      {
        error: "no live session",
        detail:
          "generated/live/telemetry.json does not exist. Start fsoc_live yourself " +
          "(see docs/PHONE_CAMERA_GOLDEN_DEMO.md) -- this route never fabricates telemetry.",
      },
      { status: 503, headers: { "cache-control": "no-store" } },
    );
  }

  try {
    const raw = await readFile(file, "utf8");
    const frame = JSON.parse(raw);
    const ageS = (Date.now() - statSync(file).mtimeMs) / 1000;
    return NextResponse.json(
      { frame, ageS, stale: ageS > 3 },
      { headers: { "cache-control": "no-store" } },
    );
  } catch (err) {
    // A partial write (fsoc_live overwrites the file every frame, non-
    // atomically) can race a read. Report it as a clean, retryable miss --
    // never as fabricated telemetry.
    return NextResponse.json(
      { error: "telemetry read failed (likely mid-write, retry)", detail: String(err) },
      { status: 503, headers: { "cache-control": "no-store" } },
    );
  }
}
