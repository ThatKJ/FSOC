import { NextRequest, NextResponse } from "next/server";
import { existsSync } from "node:fs";
import { readFile, rename, writeFile } from "node:fs/promises";
import path from "node:path";

import { isValidRecordingId, recordingDir } from "@/lib/real-sessions/paths";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

/**
 * SERVER-ONLY. G2 annotation tool: reviewed labels for one recording, stored as a
 * single labels.json keyed by frameIndex (not JSONL -- a human re-labeling a frame
 * must UPDATE that frame's entry, not append a second, ambiguous one).
 *
 * `reviewed` is always true for a label written through this route: a human
 * explicitly saved it. A detector's own suggestion (from telemetry.jsonl,
 * fetched separately) is never written here on its own -- it only becomes a
 * label once a person accepts/adjusts it and hits Save, matching the "detector
 * suggestions are not independent ground truth until reviewed" requirement.
 *
 * GET  /api/real-sessions/:recordingId/labels
 *   200  { schemaVersion, recordingId, sessionId, labelCoordinateSpace, labels }
 *        (labels: {} if none saved yet -- never fabricated)
 *
 * POST /api/real-sessions/:recordingId/labels
 *   body: { frameIndex: number, presence: "present"|"absent"|"partial_occlusion"|
 *           "full_occlusion"|"ambiguous", centerXPx?: number|null, centerYPx?: number|null }
 *   200  the updated labels document
 *   400  invalid recordingId/body
 *   404  no such recording
 */

type Presence = "present" | "absent" | "partial_occlusion" | "full_occlusion" | "ambiguous";
const PRESENCE_VALUES: Presence[] = ["present", "absent", "partial_occlusion", "full_occlusion", "ambiguous"];

interface FrameLabel {
  presence: Presence;
  centerXPx: number | null;
  centerYPx: number | null;
  reviewed: true;
  labeledAtEpochMs: number;
}

interface LabelsDoc {
  schemaVersion: 1;
  recordingId: string;
  sessionId: string;
  labelCoordinateSpace: "raw";
  labels: Record<string, FrameLabel>;
}

function labelsPath(recordingId: string): string {
  return path.join(recordingDir(recordingId), "labels.json");
}

async function readManifestSessionId(recordingId: string): Promise<string> {
  try {
    const manifest = JSON.parse(await readFile(path.join(recordingDir(recordingId), "manifest.json"), "utf8"));
    return manifest.sessionId ?? "";
  } catch {
    return "";
  }
}

async function readLabelsDoc(recordingId: string): Promise<LabelsDoc> {
  const file = labelsPath(recordingId);
  if (existsSync(file)) {
    try {
      return JSON.parse(await readFile(file, "utf8"));
    } catch {
      // fall through to a fresh doc rather than serving corrupt content
    }
  }
  return {
    schemaVersion: 1,
    recordingId,
    sessionId: await readManifestSessionId(recordingId),
    labelCoordinateSpace: "raw",
    labels: {},
  };
}

export async function GET(_req: NextRequest, { params }: { params: { recordingId: string } }) {
  const { recordingId } = params;
  if (!isValidRecordingId(recordingId)) {
    return NextResponse.json({ error: "invalid recordingId" }, { status: 400 });
  }
  if (!existsSync(recordingDir(recordingId))) {
    return NextResponse.json({ error: "recording not found" }, { status: 404 });
  }
  return NextResponse.json(await readLabelsDoc(recordingId), { headers: { "cache-control": "no-store" } });
}

export async function POST(req: NextRequest, { params }: { params: { recordingId: string } }) {
  const { recordingId } = params;
  if (!isValidRecordingId(recordingId)) {
    return NextResponse.json({ error: "invalid recordingId" }, { status: 400 });
  }
  if (!existsSync(recordingDir(recordingId))) {
    return NextResponse.json({ error: "recording not found" }, { status: 404 });
  }

  let body: unknown;
  try {
    body = await req.json();
  } catch (err) {
    return NextResponse.json({ error: "invalid JSON body", detail: String(err) }, { status: 400 });
  }

  const b = body as {
    frameIndex?: unknown;
    presence?: unknown;
    centerXPx?: unknown;
    centerYPx?: unknown;
  } | null;
  const frameIndex = b?.frameIndex;
  const presence = b?.presence;
  if (typeof frameIndex !== "number" || !Number.isInteger(frameIndex) || frameIndex < 0) {
    return NextResponse.json({ error: "frameIndex must be a non-negative integer" }, { status: 400 });
  }
  if (typeof presence !== "string" || !PRESENCE_VALUES.includes(presence as Presence)) {
    return NextResponse.json({ error: `presence must be one of: ${PRESENCE_VALUES.join(", ")}` }, { status: 400 });
  }

  const centerXRaw = b?.centerXPx;
  const centerYRaw = b?.centerYPx;
  const centerProvided =
    typeof centerXRaw === "number" && Number.isFinite(centerXRaw) &&
    typeof centerYRaw === "number" && Number.isFinite(centerYRaw);

  // A visible target needs a center; an absent/fully-occluded one cannot have one --
  // enforced server-side so a client bug can't write a self-contradictory label.
  if ((presence === "present" || presence === "partial_occlusion") && !centerProvided) {
    return NextResponse.json(
      { error: `presence "${presence}" requires numeric centerXPx/centerYPx` },
      { status: 400 },
    );
  }
  const forceNull = presence === "absent" || presence === "full_occlusion";
  const centerXPx = forceNull ? null : centerProvided ? (centerXRaw as number) : null;
  const centerYPx = forceNull ? null : centerProvided ? (centerYRaw as number) : null;

  const doc = await readLabelsDoc(recordingId);
  doc.labels[String(frameIndex)] = {
    presence: presence as Presence,
    centerXPx,
    centerYPx,
    reviewed: true,
    labeledAtEpochMs: Date.now(),
  };

  const finalPath = labelsPath(recordingId);
  const tmpPath = `${finalPath}.tmp`;
  await writeFile(tmpPath, JSON.stringify(doc, null, 2), "utf8");
  await rename(tmpPath, finalPath);

  return NextResponse.json(doc, { headers: { "cache-control": "no-store" } });
}
