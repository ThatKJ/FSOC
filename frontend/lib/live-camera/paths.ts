import path from "node:path";

/** repo root — `frontend/` lives directly under it. */
export function repoRoot(): string {
  return path.resolve(process.cwd(), "..");
}

/**
 * Where fsoc_live publishes manifest.json / frame_<N>.jpg / telemetry_<N>.json /
 * command.txt. Override with FSOC_LIVE_OUT (must match fsoc_live's own --live-out).
 */
export function liveDir(): string {
  const override = process.env.FSOC_LIVE_OUT;
  return override
    ? path.isAbsolute(override)
      ? override
      : path.resolve(repoRoot(), override)
    : path.resolve(repoRoot(), "generated", "live");
}
