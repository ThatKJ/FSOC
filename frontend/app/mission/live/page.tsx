"use client";

import { useEffect, useMemo, useRef, useState } from "react";

import Link from "next/link";

import { Screen } from "@/components/shell/AppShell";
import { Panel, PanelHeader, KeyValueRow, StatusSquare } from "@/components/ui/Panel";
import { deg, fixed, px } from "@/lib/format";

/** Button-styled link (not a real <button>, so it's safe to use inside/as an <a>). */
function linkButtonClass(variant: "ghost" | "outline") {
  const base =
    "inline-flex items-center justify-center gap-margin-sm border px-margin-md py-margin-sm font-headline-sm text-headline-sm uppercase tracking-wider transition-colors";
  return variant === "ghost"
    ? `${base} border-outline-variant text-on-surface hover:border-primary hover:text-primary bg-surface`
    : `${base} border-outline-variant text-on-surface hover:bg-surface-container hover:border-primary/60`;
}

/** Same visual language as linkButtonClass, for real <button> recording controls. */
function actionButtonClass(tone: "primary" | "warning" | "muted" = "muted") {
  const base =
    "inline-flex flex-1 items-center justify-center gap-margin-sm border px-margin-md py-margin-sm font-headline-sm text-headline-sm uppercase tracking-wider transition-colors disabled:cursor-not-allowed disabled:opacity-40";
  if (tone === "primary") return `${base} border-primary text-primary hover:bg-primary/10`;
  if (tone === "warning") return `${base} border-tertiary text-tertiary-container hover:bg-tertiary/10`;
  return `${base} border-outline-variant text-on-surface hover:bg-surface-container-low`;
}

/**
 * /mission/live — Mobile Phone Camera-in-the-Loop viewer.
 *
 * NOT one of the 9 Stitch nav screens (lib/nav.ts is deliberately fixed at 9
 * — see STITCH_IMPLEMENTATION_MAP.md); reachable by direct link only, nested
 * under Mission Control rather than a new top-level concept. Reuses the same
 * Panel/KeyValueRow primitives the rest of Mission Control uses.
 *
 * Polls /api/live-camera and /api/live-camera/frame on an interval -- this is
 * an honest snapshot-file poll of whatever fsoc_live (a separate, long-running
 * process YOU start yourself) last wrote, not a push/streaming connection.
 * See docs/PHONE_CAMERA_METRICS.md "Mission Control transport".
 *
 * Frame/telemetry identity: /api/live-camera returns the frameIndex its
 * telemetry belongs to; the image is fetched by that EXACT index from
 * /api/live-camera/frame?frame=<index> (see include/fsoc/live_frame_publisher.hpp).
 * This page never shows an image fetched independently of the telemetry that
 * named it -- a 404 on that fetch just skips the image update for this tick.
 */

interface LiveFrame {
  schemaVersion: number;
  sessionId: string;
  frameIndex: number;
  timestampS: number;
  dtS: number;
  cameraSource: string;
  actuatorType: string;
  sourceKind: string;
  sourceBackend: string;
  sourceDescription: string;
  rawWidthPx: number;
  rawHeightPx: number;
  preprocessedWidthPx: number;
  preprocessedHeightPx: number;
  perceptionMode: string;
  perceptionSource: string;
  classicalDetected: boolean;
  aiCandidateDetected: boolean;
  aiPresenceProbability: number | null;
  targetDetected: boolean;
  detectedXPx: number | null;
  detectedYPx: number | null;
  pixelErrorXPx: number | null;
  pixelErrorYPx: number | null;
  panErrorDeg: number | null;
  tiltErrorDeg: number | null;
  totalErrorDeg: number | null;
  calibrationStatus: string;
  lockState: string;
  trackerConfidence: number;
  isPrediction: boolean;
  controlEnabled: boolean;
  commandPanRateDegS: number;
  commandTiltRateDegS: number;
  virtualPanDeg: number;
  virtualTiltDeg: number;
  virtualPanSaturated: boolean;
  virtualTiltSaturated: boolean;
  recordingActive: boolean;
  recordingId: string | null;
  recordedFrameCount: number;
  recordingErrorCount: number;
}

// Was 500ms. fsoc_live processes/publishes at the camera's native rate (a webcam is
// typically 15-30fps, i.e. a new frame every 33-66ms) -- polling only every 500ms
// means this page shows at most ~2fps no matter how fast the C++ side runs, silently
// dropping the rest. 200ms (~5fps) is a small, low-risk tightening (still one HTTP
// round trip per poll to a local server reading local disk) that noticeably improves
// how usable the demo looks without approaching the native rate. It does NOT close
// the gap to native FPS -- see the TIMING panel below, which reports the real
// measured processed/displayed rates so this limitation stays visible, not hidden.
const POLL_MS = 200;

export default function LiveCameraPage() {
  const [frame, setFrame] = useState<LiveFrame | null>(null);
  const [ageS, setAgeS] = useState<number | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [frameSrc, setFrameSrc] = useState<string>("");
  const [recordCommandPending, setRecordCommandPending] = useState(false);
  const [recordCommandError, setRecordCommandError] = useState<string | null>(null);
  const [lastEventLabel, setLastEventLabel] = useState<string | null>(null);
  const [displayedFps, setDisplayedFps] = useState<number | null>(null);
  const mountedRef = useRef(true);
  const lastSessionIdRef = useRef<string | null>(null);
  const frameSrcRef = useRef<string>("");
  // Timestamps (performance.now(), ms) of the last few times a NEW image was
  // actually displayed -- i.e. a distinct fetched frame, not a repeated poll that
  // 404'd or returned the same bytes. This is a real measured client-side rate, not
  // derived from the C++ side's own dtS (which is a different, native-processing
  // quantity -- see the PROCESSED row below).
  const displayTimestampsRef = useRef<number[]>([]);
  const imgRef = useRef<HTMLImageElement>(null);

  useEffect(() => {
    mountedRef.current = true;
    let timer: ReturnType<typeof setTimeout>;

    async function poll() {
      try {
        const res = await fetch("/api/live-camera", { cache: "no-store" });
        const body = await res.json();
        if (!mountedRef.current) return;
        if (res.ok) {
          const nextFrame: LiveFrame = body.frame;
          // A new sessionId (fsoc_live restarted -- a reconnect) must not inherit the
          // previous session's displayed image: drop it until the new session's own
          // first frame is confirmed fetched below.
          if (lastSessionIdRef.current !== null && lastSessionIdRef.current !== nextFrame.sessionId) {
            if (frameSrcRef.current.startsWith("blob:")) URL.revokeObjectURL(frameSrcRef.current);
            frameSrcRef.current = "";
            setFrameSrc("");
            displayTimestampsRef.current = [];
            setDisplayedFps(null);
          }
          lastSessionIdRef.current = nextFrame.sessionId;
          setFrame(nextFrame);
          setAgeS(body.ageS);
          setError(null);

          // Fetch the image by the EXACT frame index the telemetry above belongs to
          // (never a bare "current frame") -- see include/fsoc/live_frame_publisher.hpp.
          // A 404 (already pruned between the two requests) just skips this tick's
          // image update rather than showing a mismatched frame.
          const frameRes = await fetch(`/api/live-camera/frame?frame=${body.frameIndex}`, {
            cache: "no-store",
          });
          if (mountedRef.current && frameRes.ok) {
            const blob = await frameRes.blob();
            if (frameSrcRef.current.startsWith("blob:")) URL.revokeObjectURL(frameSrcRef.current);
            frameSrcRef.current = URL.createObjectURL(blob);
            setFrameSrc(frameSrcRef.current);

            const now = performance.now();
            const timestamps = [...displayTimestampsRef.current, now].slice(-10);
            displayTimestampsRef.current = timestamps;
            if (timestamps.length >= 2) {
              const spanS = (timestamps[timestamps.length - 1] - timestamps[0]) / 1000;
              setDisplayedFps(spanS > 0 ? (timestamps.length - 1) / spanS : null);
            }
          }
        } else {
          setError(body.detail ?? body.error ?? "unknown error");
        }
      } catch (err) {
        if (mountedRef.current) setError(String(err));
      } finally {
        if (mountedRef.current) timer = setTimeout(poll, POLL_MS);
      }
    }
    poll();

    return () => {
      mountedRef.current = false;
      clearTimeout(timer);
      if (frameSrcRef.current.startsWith("blob:")) URL.revokeObjectURL(frameSrcRef.current);
    };
  }, []);

  const stale = ageS != null && ageS > 3;

  // Position a raw-pixel-space point (detector centroid) as a CSS overlay on top of
  // the displayed <img> -- recomputed on every frame since the poll can resize/replace
  // the image at any time. object-contain with only max-h/max-w constraints means the
  // <img> element's own box already hugs the rendered image content (no letterbox gap
  // to additionally account for), same approach as /mission/annotate.
  const toOverlayCss = useMemo(() => {
    if (!imgRef.current || !frame) return null;
    const el = imgRef.current;
    // offsetLeft/offsetTop/offsetWidth/offsetHeight are relative to the nearest
    // positioned ancestor (the `relative` container below, which IS the img's
    // offsetParent) and already account for the flex-centering gap when the
    // image doesn't fill the container (object-contain letterboxing) --
    // getBoundingClientRect() gives viewport coordinates, which silently drops
    // that offset and was why the reticle sat near the container's corner
    // instead of on the actual beacon.
    const w = el.offsetWidth;
    const h = el.offsetHeight;
    if (w === 0 || h === 0) return null;
    return (xRaw: number, yRaw: number) => ({
      left: `${el.offsetLeft + (xRaw / frame.rawWidthPx) * w}px`,
      top: `${el.offsetTop + (yRaw / frame.rawHeightPx) * h}px`,
    });
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [frame?.frameIndex, frameSrc]);

  // Sends a G2 recording command. A 200 here only means the command file was
  // written -- it is NOT proof fsoc_live applied it. This page's recording status
  // display always reflects the LAST POLLED telemetry (frame.recordingActive etc.),
  // never local optimistic state, matching the honesty rule the rest of this page
  // already follows for the camera feed itself.
  async function sendRecordCommand(action: "start" | "stop" | "mark_event", label?: string) {
    setRecordCommandPending(true);
    setRecordCommandError(null);
    try {
      const res = await fetch("/api/live-camera/record", {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify(label ? { action, label } : { action }),
      });
      const body = await res.json();
      if (!res.ok) {
        setRecordCommandError(body.detail ?? body.error ?? "command failed");
      } else if (action === "mark_event" && label) {
        setLastEventLabel(label);
      }
    } catch (err) {
      setRecordCommandError(String(err));
    } finally {
      if (mountedRef.current) setRecordCommandPending(false);
    }
  }

  return (
    <Screen pad>
      <div className="flex items-center justify-between border border-outline-variant bg-surface-container px-margin-md py-margin-sm">
        <div className="flex items-center gap-margin-md">
          <StatusSquare active={!!frame && !stale} color={frame && !stale ? "detected" : "lost"} pulse />
          <span className="font-headline-sm text-headline-sm uppercase tracking-wider text-primary">
            Mobile Phone Camera-in-the-Loop
          </span>
        </div>
        <span className="font-label-xs text-label-xs uppercase tracking-widest text-on-surface-variant">
          Polling /api/live-camera every {POLL_MS}ms — not a physical closed loop
        </span>
      </div>

      {!frame && (
        <Panel className="mt-margin-md flex flex-1 flex-col items-center justify-center gap-margin-md p-margin-lg text-center">
          <StatusSquare active={false} color="muted" />
          <span className="font-headline-sm text-headline-sm uppercase tracking-widest text-on-surface">
            Real-Camera Mode
          </span>
          <p className="max-w-lg font-data-mono text-data-mono text-on-surface-variant">
            This mode runs on your own machine because camera frames are processed by the
            native FSOC C++ engine (<span className="text-on-surface">fsoc_live</span>), not by
            this web server. This page polls a local telemetry file and will never fabricate a
            reading — it shows this state honestly instead.
          </p>
          <span className="max-w-md font-data-mono text-[11px] text-on-surface-variant">
            {error ?? "Waiting for fsoc_live..."}
          </span>
          <div className="mt-margin-sm flex flex-wrap items-center justify-center gap-margin-sm">
            <Panel className="bg-surface px-margin-md py-margin-sm">
              <code className="font-data-mono text-data-mono text-primary">./run_fsoc.sh phone</code>
            </Panel>
            <a
              href="https://github.com/ThatKJ/FSOC/blob/main/docs/PHONE_CAMERA_GOLDEN_DEMO.md"
              target="_blank"
              rel="noreferrer"
              className={linkButtonClass("ghost")}
            >
              Setup Guide
            </a>
            <Link href="/mission" className={linkButtonClass("outline")}>
              View Public Replay
            </Link>
            <Link href="/mission/annotate" className={linkButtonClass("outline")}>
              Review Recordings
            </Link>
          </div>
        </Panel>
      )}

      {frame && (
        <div className="mt-margin-md flex flex-1 gap-margin-md overflow-hidden">
          <Panel className="flex flex-1 flex-col overflow-hidden">
            <PanelHeader
              title="Camera Feed"
              accent={stale ? "warning" : "primary"}
              right={
                <span className="font-data-mono text-[11px] text-on-surface-variant">
                  {stale ? `STALE (${fixed(ageS, 1)}s)` : "LIVE"}
                </span>
              }
            />
            <div className="relative flex flex-1 items-center justify-center overflow-hidden bg-black">
              {frameSrc && (
                // eslint-disable-next-line @next/next/no-img-element -- polled, cache-busted local snapshot, not an optimizable static asset
                <img
                  ref={imgRef}
                  src={frameSrc}
                  alt="Live camera frame"
                  className="max-h-full max-w-full object-contain"
                />
              )}
              {toOverlayCss && frame?.detectedXPx != null && frame?.detectedYPx != null && (
                <span
                  className="pointer-events-none absolute h-6 w-6 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-secondary shadow-[0_0_0_1px_rgba(0,0,0,0.6)]"
                  style={toOverlayCss(frame.detectedXPx, frame.detectedYPx)}
                  title="detected beacon centroid"
                >
                  <span className="absolute left-1/2 top-1/2 h-px w-3 -translate-x-1/2 -translate-y-1/2 bg-secondary" />
                  <span className="absolute left-1/2 top-1/2 h-3 w-px -translate-x-1/2 -translate-y-1/2 bg-secondary" />
                </span>
              )}
            </div>
          </Panel>

          <aside className="flex w-[340px] shrink-0 flex-col gap-gutter overflow-y-auto bg-outline-variant">
            <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
              <span className="mb-unit font-label-xs text-label-xs text-primary">PROVENANCE</span>
              <KeyValueRow k="CAMERA SOURCE" v={frame.cameraSource} tone="primary" />
              <KeyValueRow k="ACTUATOR" v={frame.actuatorType} tone="warning" />
              <KeyValueRow
                k="CALIBRATION"
                v={frame.calibrationStatus === "CALIBRATED" ? "CALIBRATED" : "UNCALIBRATED (pixel-only)"}
                tone={frame.calibrationStatus === "CALIBRATED" ? "primary" : "warning"}
              />
              <KeyValueRow k="BACKEND" v={frame.sourceBackend} />
              <KeyValueRow k="SESSION ID" v={frame.sessionId} />
              <KeyValueRow
                k="RAW / PROCESSED"
                v={`${frame.rawWidthPx}x${frame.rawHeightPx} -> ${frame.preprocessedWidthPx}x${frame.preprocessedHeightPx}`}
                border={false}
              />
            </div>

            <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
              <span className="mb-unit font-label-xs text-label-xs text-on-surface-variant">PERCEPTION</span>
              <KeyValueRow k="MODE" v={frame.perceptionMode} />
              <KeyValueRow
                k="SOURCE"
                v={frame.perceptionSource}
                tone={frame.perceptionSource === "NONE" ? "lost" : "primary"}
              />
              <KeyValueRow
                k="AI CONF"
                v={frame.aiPresenceProbability != null ? fixed(frame.aiPresenceProbability, 3) : "—"}
                border={false}
              />
            </div>

            <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
              <span className="mb-unit font-label-xs text-label-xs text-on-surface-variant">
                STATE ESTIMATOR
              </span>
              <KeyValueRow
                k="LOCK STATE"
                v={frame.lockState}
                tone={
                  frame.lockState === "TRACKING" ? "primary" : frame.lockState === "COASTING" ? "warning" : "lost"
                }
              />
              <KeyValueRow k="CONFIDENCE" v={fixed(frame.trackerConfidence, 2)} />
              <KeyValueRow k="POSITION" v={frame.isPrediction ? "PREDICTED" : "MEASURED"} border={false} />
            </div>

            <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
              <span className="mb-unit font-label-xs text-label-xs text-on-surface-variant">TIMING</span>
              <KeyValueRow
                k="PROCESSED FPS"
                v={frame.dtS > 0 ? fixed(1 / frame.dtS, 1) : "—"}
              />
              <KeyValueRow k="DISPLAYED FPS (this page)" v={displayedFps != null ? fixed(displayedFps, 1) : "—"} />
              <KeyValueRow
                k="POLL INTERVAL"
                v={`${POLL_MS}ms client poll`}
                border={false}
              />
              <span className="pt-unit font-data-mono text-[10px] leading-tight text-on-surface-variant">
                PROCESSED FPS = 1 / dtS, the C++ side&apos;s own measured wall-clock interval
                between frames (real per-frame timing, not an exposure timestamp). DISPLAYED FPS
                is this browser tab&apos;s measured image-update rate, capped by the poll interval
                above regardless of how fast the camera runs.
              </span>
            </div>

            <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
              <span className="mb-unit font-label-xs text-label-xs text-tertiary-container">
                POINTING ERROR
              </span>
              <KeyValueRow
                k="PIXEL [px]"
                v={
                  frame.pixelErrorXPx != null && frame.pixelErrorYPx != null
                    ? px(Math.hypot(frame.pixelErrorXPx, frame.pixelErrorYPx))
                    : "—"
                }
                tone="warning"
              />
              <KeyValueRow k="PAN [deg]" v={deg(frame.panErrorDeg)} tone="warning" />
              <KeyValueRow k="TILT [deg]" v={deg(frame.tiltErrorDeg)} tone="warning" border={false} />
            </div>

            <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
              <span className="mb-unit font-label-xs text-label-xs text-on-surface-variant">
                VIRTUAL ACTUATOR (bookkeeping only)
              </span>
              <KeyValueRow k="CONTROL" v={frame.controlEnabled ? "ENABLED" : "OBSERVE-ONLY"} />
              <KeyValueRow k="CMD RATE [deg/s]" v={`${fixed(frame.commandPanRateDegS, 2)} / ${fixed(frame.commandTiltRateDegS, 2)}`} />
              <KeyValueRow k="VIRTUAL ANGLE [deg]" v={`${fixed(frame.virtualPanDeg, 1)} / ${fixed(frame.virtualTiltDeg, 1)}`} border={false} />
            </div>

            <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
              <span className="mb-unit font-label-xs text-label-xs text-secondary">
                RECORDING (G2)
              </span>
              <KeyValueRow
                k="STATUS"
                v={frame.recordingActive ? `RECORDING (${frame.recordingId ?? "?"})` : "NOT RECORDING"}
                tone={frame.recordingActive ? "primary" : "muted"}
              />
              {frame.recordingActive && (
                <KeyValueRow
                  k="FRAMES / ERRORS"
                  v={`${frame.recordedFrameCount} / ${frame.recordingErrorCount}`}
                />
              )}
              <div className="flex gap-margin-sm py-margin-sm">
                <button
                  type="button"
                  disabled={recordCommandPending || frame.recordingActive}
                  onClick={() => sendRecordCommand("start")}
                  className={actionButtonClass("primary")}
                >
                  Start Recording
                </button>
                <button
                  type="button"
                  disabled={recordCommandPending || !frame.recordingActive}
                  onClick={() => sendRecordCommand("stop")}
                  className={actionButtonClass("warning")}
                >
                  Stop Recording
                </button>
              </div>
              <div className="flex flex-wrap gap-margin-sm pb-margin-sm">
                <button
                  type="button"
                  disabled={recordCommandPending || !frame.recordingActive}
                  onClick={() => sendRecordCommand("mark_event", "beacon_covered")}
                  className={actionButtonClass("muted")}
                >
                  Mark: Covered
                </button>
                <button
                  type="button"
                  disabled={recordCommandPending || !frame.recordingActive}
                  onClick={() => sendRecordCommand("mark_event", "beacon_visible")}
                  className={actionButtonClass("muted")}
                >
                  Mark: Visible
                </button>
                <button
                  type="button"
                  disabled={recordCommandPending || !frame.recordingActive}
                  onClick={() => sendRecordCommand("mark_event", "scene_change")}
                  className={actionButtonClass("muted")}
                >
                  Mark: Scene Change
                </button>
              </div>
              {lastEventLabel && !recordCommandError && (
                <span className="pb-margin-sm font-data-mono text-[11px] text-on-surface-variant">
                  last marked: {lastEventLabel}
                </span>
              )}
              {recordCommandError && (
                <span className="pb-margin-sm font-data-mono text-[11px] text-error">{recordCommandError}</span>
              )}
            </div>
          </aside>
        </div>
      )}
    </Screen>
  );
}
