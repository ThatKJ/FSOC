import { NextResponse } from "next/server";
import { existsSync } from "node:fs";
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";

import { isValidRecordingId, realSessionsDir, recordingDir } from "@/lib/real-sessions/paths";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

/**
 * SERVER-ONLY. G2 annotation tool: lists local recordings written by
 * fsoc_live's RealSessionRecorder (fsoc/real_session_recorder.hpp) so the
 * annotator page can offer a picker. Read-only; never fabricates a recording
 * that doesn't exist on disk.
 *
 * GET /api/real-sessions
 *   200  [{ recordingId, sessionId, startedAtEpochMs, endedAtEpochMs, recordedFrameCount,
 *           errorCount, eventCount, calibrationStatus, perceptionMode }], newest first
 */

interface Manifest {
  recordingId: string;
  sessionId: string;
  startedAtEpochMs: number;
  endedAtEpochMs: number | null;
  recordedFrameCount: number;
  errorCount: number;
  eventCount: number;
  calibrationStatus: string;
  perceptionMode: string;
}

export async function GET() {
  const dir = realSessionsDir();
  if (!existsSync(dir)) {
    return NextResponse.json([], { headers: { "cache-control": "no-store" } });
  }

  const entries = await readdir(dir, { withFileTypes: true });
  const recordings: Manifest[] = [];
  for (const entry of entries) {
    if (!entry.isDirectory() || !isValidRecordingId(entry.name)) continue;
    const manifestPath = path.join(recordingDir(entry.name), "manifest.json");
    if (!existsSync(manifestPath)) continue;
    try {
      const manifest = JSON.parse(await readFile(manifestPath, "utf8"));
      recordings.push(manifest);
    } catch {
      // A recording mid-write (manifest temp-then-rename raced this read) is skipped
      // for this listing, not reported as broken -- it will appear once settled.
      continue;
    }
  }
  recordings.sort((a, b) => b.startedAtEpochMs - a.startedAtEpochMs);
  return NextResponse.json(recordings, { headers: { "cache-control": "no-store" } });
}
