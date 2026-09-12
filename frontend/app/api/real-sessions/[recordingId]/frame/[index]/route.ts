import { NextResponse } from "next/server";
import { existsSync } from "node:fs";
import { readFile } from "node:fs/promises";
import path from "node:path";

import { isValidRecordingId, recordingDir } from "@/lib/real-sessions/paths";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

/**
 * SERVER-ONLY. G2 annotation tool: serves one RAW frame from a recording
 * (frames/frame_<index>.jpg, exactly as fsoc_live wrote it -- no overlay, no
 * crosshair, no dashboard text; see fsoc/real_session_recorder.hpp).
 *
 * GET /api/real-sessions/:recordingId/frame/:index
 *   200  image/jpeg bytes
 *   400  invalid recordingId/index
 *   404  no such recording or frame
 */

export async function GET(
  _req: Request,
  { params }: { params: { recordingId: string; index: string } },
) {
  const { recordingId, index } = params;
  if (!isValidRecordingId(recordingId)) {
    return NextResponse.json({ error: "invalid recordingId" }, { status: 400 });
  }
  const frameIndex = Number(index);
  if (!Number.isInteger(frameIndex) || frameIndex < 0) {
    return NextResponse.json({ error: "invalid frame index" }, { status: 400 });
  }

  const file = path.join(recordingDir(recordingId), "frames", `frame_${frameIndex}.jpg`);
  if (!existsSync(file)) {
    return NextResponse.json({ error: "frame not found" }, { status: 404 });
  }
  const bytes = await readFile(file);
  return new NextResponse(bytes, {
    status: 200,
    headers: { "content-type": "image/jpeg", "cache-control": "no-store" },
  });
}
