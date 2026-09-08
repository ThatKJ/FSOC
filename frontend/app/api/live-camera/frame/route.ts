import { NextResponse } from "next/server";
import { readFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import path from "node:path";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

/**
 * SERVER-ONLY. Mobile Phone Camera-in-the-Loop milestone.
 *
 * Serves the most recent JPEG fsoc_live wrote to generated/live/frame.jpg.
 * Polling this on an interval (with a cache-busting query param) is the
 * "MJPEG-via-polling" pattern this milestone uses instead of a WebSocket/
 * MJPEG server -- see docs/PHONE_CAMERA_METRICS.md "Mission Control
 * transport" for why, and its honestly-disclosed latency characteristics.
 *
 * GET /api/live-camera/frame
 *   200  image/jpeg bytes
 *   503  no live session / no frame written yet
 */

function repoRoot(): string {
  return path.resolve(process.cwd(), "..");
}

function framePath(): string {
  const override = process.env.FSOC_LIVE_OUT;
  const dir = override ? (path.isAbsolute(override) ? override : path.resolve(repoRoot(), override))
                        : path.resolve(repoRoot(), "generated", "live");
  return path.join(dir, "frame.jpg");
}

export async function GET() {
  const file = framePath();
  if (!existsSync(file)) {
    return NextResponse.json(
      { error: "no live frame", detail: "generated/live/frame.jpg does not exist yet." },
      { status: 503, headers: { "cache-control": "no-store" } },
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
      { error: "frame read failed (likely mid-write, retry)", detail: String(err) },
      { status: 503, headers: { "cache-control": "no-store" } },
    );
  }
}
