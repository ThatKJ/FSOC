import { NextResponse } from "next/server";
import { existsSync } from "node:fs";
import { readFile } from "node:fs/promises";
import path from "node:path";

import { isValidRecordingId, recordingDir } from "@/lib/real-sessions/paths";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

/**
 * SERVER-ONLY. G2 annotation tool: one recording's manifest plus its full
 * per-frame telemetry (parsed from telemetry.jsonl), so the annotator page can
 * build a frame scrubber and show the detector's own (non-ground-truth)
 * suggested centroid alongside each frame.
 *
 * GET /api/real-sessions/:recordingId
 *   200  { manifest, telemetry: [...] }  -- telemetry sorted by frameIndex
 *   400  invalid recordingId
 *   404  no such recording
 */

export async function GET(_req: Request, { params }: { params: { recordingId: string } }) {
  const { recordingId } = params;
  if (!isValidRecordingId(recordingId)) {
    return NextResponse.json({ error: "invalid recordingId" }, { status: 400 });
  }
  const dir = recordingDir(recordingId);
  const manifestPath = path.join(dir, "manifest.json");
  if (!existsSync(manifestPath)) {
    return NextResponse.json({ error: "recording not found" }, { status: 404 });
  }

  const manifest = JSON.parse(await readFile(manifestPath, "utf8"));

  const telemetryPath = path.join(dir, "telemetry.jsonl");
  const telemetry: unknown[] = [];
  if (existsSync(telemetryPath)) {
    const raw = await readFile(telemetryPath, "utf8");
    for (const line of raw.split("\n")) {
      if (!line.trim()) continue;
      try {
        telemetry.push(JSON.parse(line));
      } catch {
        // One malformed line (e.g. a truncated last write from an unclean stop) is
        // skipped, not fatal to the rest of the recording's review.
        continue;
      }
    }
  }
  telemetry.sort((a, b) => (a as { frameIndex: number }).frameIndex - (b as { frameIndex: number }).frameIndex);

  return NextResponse.json({ manifest, telemetry }, { headers: { "cache-control": "no-store" } });
}
