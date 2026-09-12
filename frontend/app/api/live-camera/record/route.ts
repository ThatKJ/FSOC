import { NextRequest, NextResponse } from "next/server";
import { existsSync } from "node:fs";
import { mkdir, rename, writeFile } from "node:fs/promises";
import path from "node:path";

import { liveDir } from "@/lib/live-camera/paths";

export const dynamic = "force-dynamic";
export const runtime = "nodejs";

/**
 * SERVER-ONLY. G2 real-session recording control (docs/LIVE_REALDATA_TASK_STATE.md).
 *
 *   browser (Start/Stop Recording, Mark Event buttons)
 *     -> POST here -> generated/live/command.txt (key=value, temp-then-rename)
 *     -> fsoc_live polls that file once per camera-frame iteration and applies it
 *     -> the NEXT /api/live-camera poll shows the applied recordingActive/
 *        recordingId/recordedFrameCount fields
 *
 * This route's 200 response means "the command file was written," never "fsoc_live
 * has applied it" -- there is no synchronous acknowledgement channel. The frontend
 * must confirm the actual effect from the next telemetry poll, the same honesty
 * rule this project applies to every other command (see fsoc/virtual_actuator.hpp's
 * comment on acknowledgement vs. measured effect).
 *
 * There is no message queue: command.txt is a single slot. Two POSTs faster than
 * one camera frame interval apart will have the earlier one silently superseded —
 * a documented limitation (see apps/fsoc_live.cpp's file-header comment), not a bug.
 *
 * POST /api/live-camera/record   body: {"action": "start"|"stop"|"mark_event", "label"?: string}
 *   200  command file written (commandId returned)
 *   400  invalid action / missing label for mark_event
 *   503  no live session running (only enforced for "start" -- fsoc_live itself
 *        also refuses to double-start, so "stop"/"mark_event" with no session is a
 *        harmless no-op on the C++ side and is not blocked here)
 */

type Action = "start" | "stop" | "mark_event";

function isAction(v: unknown): v is Action {
  return v === "start" || v === "stop" || v === "mark_event";
}

export async function POST(req: NextRequest) {
  let body: unknown;
  try {
    body = await req.json();
  } catch (err) {
    return NextResponse.json({ error: "invalid JSON body", detail: String(err) }, { status: 400 });
  }

  const action = (body as { action?: unknown } | null)?.action;
  const label = (body as { label?: unknown } | null)?.label;
  if (!isAction(action)) {
    return NextResponse.json({ error: "action must be one of: start, stop, mark_event" }, { status: 400 });
  }
  if (action === "mark_event" && (typeof label !== "string" || label.trim().length === 0)) {
    return NextResponse.json({ error: "mark_event requires a non-empty string label" }, { status: 400 });
  }

  const dir = liveDir();
  if (action === "start" && !existsSync(path.join(dir, "manifest.json"))) {
    return NextResponse.json(
      {
        error: "no live session",
        detail: "generated/live/manifest.json does not exist -- start fsoc_live before recording.",
      },
      { status: 503, headers: { "cache-control": "no-store" } },
    );
  }

  await mkdir(dir, { recursive: true });
  const commandId = Date.now().toString();
  const lines = [`commandId=${commandId}`, `action=${action}`];
  // command.txt is line-oriented key=value (see apps/fsoc_live.cpp) -- strip any
  // newline a caller's label might contain so it can't inject a bogus extra key.
  if (action === "mark_event") lines.push(`label=${(label as string).replace(/[\r\n]+/g, " ").trim()}`);
  const finalPath = path.join(dir, "command.txt");
  const tmpPath = `${finalPath}.tmp`;
  await writeFile(tmpPath, lines.join("\n") + "\n", "utf8");
  await rename(tmpPath, finalPath);

  return NextResponse.json(
    { accepted: true, commandId, action },
    { headers: { "cache-control": "no-store" } },
  );
}
