import { NextRequest, NextResponse } from "next/server";
import { readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import path from "node:path";

import { liveDir } from "@/lib/live-camera/paths";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

/**
 * SERVER-ONLY. Mobile Phone Camera-in-the-Loop milestone.
 *
 * Serves one specific frame_<N>.jpg that LiveFramePublisher wrote (see
 * include/fsoc/live_frame_publisher.hpp). The caller MUST pass the exact
 * frame index it got from GET /api/live-camera's `frameIndex` field --
 * there is no "current frame" concept here on purpose, so an image can
 * never be served mismatched against the telemetry that named it.
 *
 * A 404 here means that pair fell outside LiveFramePublisher's retention
 * window between the two requests (the client polled too slowly) -- the
 * correct response is to re-poll /api/live-camera for a fresher index, not
 * to fall back to any other image.
 *
 * GET /api/live-camera/frame?frame=<index>
 *   200  image/jpeg bytes for exactly that frame index
 *   400  missing/invalid ?frame=
 *   404  that frame index was never published, or has already been pruned
 *   503  no live session / output directory does not exist yet
 */

export async function GET(req: NextRequest) {
  const dir = liveDir();
  if (!existsSync(dir)) {
    return NextResponse.json(
      { error: "no live session", detail: `${dir} does not exist yet.` },
      { status: 503, headers: { "cache-control": "no-store" } },
    );
  }

  const frameParam = new URL(req.url).searchParams.get("frame");
  const frameIndex = frameParam !== null ? Number(frameParam) : NaN;
  if (!Number.isInteger(frameIndex) || frameIndex < 0) {
    return NextResponse.json(
      { error: "missing or invalid ?frame=<non-negative integer>" },
      { status: 400, headers: { "cache-control": "no-store" } },
    );
  }

  // Reject path traversal / non-numeric injection outright: the filename is built
  // from a validated integer only, never from the raw query string.
  const file = path.join(dir, `frame_${frameIndex}.jpg`);
  if (!existsSync(file)) {
    return NextResponse.json(
      {
        error: "frame not found",
        detail: `frame ${frameIndex} was never published or has already been pruned -- re-fetch /api/live-camera for a current index.`,
      },
      { status: 404, headers: { "cache-control": "no-store" } },
    );
  }
  try {
    const bytes = await readFile(file);
    return new NextResponse(bytes, {
      status: 200,
      headers: { "content-type": "image/jpeg", "cache-control": "no-store" },
    });
  } catch (err) {
    return NextResponse.json(
      { error: "frame read failed (likely mid-prune, retry)", detail: String(err) },
      { status: 503, headers: { "cache-control": "no-store" } },
    );
  }
}
