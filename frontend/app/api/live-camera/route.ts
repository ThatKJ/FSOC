import { NextResponse } from "next/server";
import { readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import path from "node:path";

import { liveDir } from "@/lib/live-camera/paths";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

/**
 * SERVER-ONLY. Mobile Phone Camera-in-the-Loop milestone.
 *
 *   fsoc_live (long-running C++ process, real camera)
 *     -> generated/live/manifest.json (names the current frame_<N>.jpg /
 *        telemetry_<N>.json pair, flipped atomically only AFTER both files
 *        are fully written -- see include/fsoc/live_frame_publisher.hpp and
 *        docs/LIVE_DATA_AUDIT.md section 2)
 *     -> (this route, polled) -> browser
 *
 * fsoc_live is a long-running process (a real camera session has no natural
 * "end of run" the way a deterministic scenario does), so it cannot be
 * invoked per-request the way /api/simulation/:scenario invokes fsoc_demo.
 * This route reads the manifest, then reads exactly the telemetry file it
 * names -- LiveFramePublisher's atomicity guarantee means that file is
 * always complete by the time the manifest can be observed naming it. The
 * frame index returned here is what the client must request from
 * /api/live-camera/frame?frame=<index> -- never a bare "current frame.jpg" --
 * so the served image and the served telemetry are guaranteed to describe
 * the same camera frame, not two independently-polled snapshots.
 *
 * GET /api/live-camera
 *   200  the current (frameIndex-identified) telemetry, plus staleness info
 *   503  no live session is running / no telemetry has been published yet
 */

interface Manifest {
  schemaVersion: number;
  frameIndex: number;
  frameFile: string;
  telemetryFile: string;
  publishedAtEpochMs: number;
}

export async function GET() {
  const dir = liveDir();
  const manifestPath = path.join(dir, "manifest.json");
  if (!existsSync(manifestPath)) {
    return NextResponse.json(
      {
        error: "no live session",
        detail:
          "generated/live/manifest.json does not exist. Start fsoc_live yourself " +
          "(see docs/PHONE_CAMERA_GOLDEN_DEMO.md) -- this route never fabricates telemetry.",
      },
      { status: 503, headers: { "cache-control": "no-store" } },
    );
  }

  let manifest: Manifest;
  try {
    manifest = JSON.parse(await readFile(manifestPath, "utf8"));
  } catch (err) {
    // Extremely unlikely (manifest is written via temp-then-rename), but a read can
    // still race a rename mid-flight on some filesystems -- report it as a clean,
    // retryable miss, never as fabricated telemetry.
    return NextResponse.json(
      { error: "manifest read failed (likely mid-write, retry)", detail: String(err) },
      { status: 503, headers: { "cache-control": "no-store" } },
    );
  }

  const telemetryPath = path.join(dir, manifest.telemetryFile);
  try {
    const frame = JSON.parse(await readFile(telemetryPath, "utf8"));
    const ageS = (Date.now() - manifest.publishedAtEpochMs) / 1000;
    return NextResponse.json(
      { frame, frameIndex: manifest.frameIndex, ageS, stale: ageS > 3 },
      { headers: { "cache-control": "no-store" } },
    );
  } catch (err) {
    return NextResponse.json(
      { error: "telemetry read failed for the manifest's current frame, retry", detail: String(err) },
      { status: 503, headers: { "cache-control": "no-store" } },
    );
  }
}
