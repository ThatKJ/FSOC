import path from "node:path";

import { repoRoot } from "@/lib/live-camera/paths";

/** Where fsoc_live's RealSessionRecorder writes recordings (--record-out). */
export function realSessionsDir(): string {
  const override = process.env.FSOC_RECORD_OUT;
  return override
    ? path.isAbsolute(override)
      ? override
      : path.resolve(repoRoot(), override)
    : path.resolve(repoRoot(), "generated", "real_sessions");
}

/** recordingId is always a wall-clock-ms token (see RealSessionRecorder) --
 *  digits-only, rejected otherwise so a URL param can never escape this directory. */
export function isValidRecordingId(id: string): boolean {
  return /^[0-9]+$/.test(id);
}

export function recordingDir(recordingId: string): string {
  return path.join(realSessionsDir(), recordingId);
}
