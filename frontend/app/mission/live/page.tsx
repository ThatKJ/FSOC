"use client";

import { useEffect, useRef, useState } from "react";

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
 */

interface LiveFrame {
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
}

const POLL_MS = 500;

export default function LiveCameraPage() {
  const [frame, setFrame] = useState<LiveFrame | null>(null);
  const [ageS, setAgeS] = useState<number | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [frameSrc, setFrameSrc] = useState<string>("");
  const mountedRef = useRef(true);

  useEffect(() => {
    mountedRef.current = true;
    let timer: ReturnType<typeof setTimeout>;

    async function poll() {
      try {
        const res = await fetch("/api/live-camera", { cache: "no-store" });
        const body = await res.json();
        if (!mountedRef.current) return;
        if (res.ok) {
          setFrame(body.frame);
          setAgeS(body.ageS);
          setError(null);
          setFrameSrc(`/api/live-camera/frame?t=${Date.now()}`);
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
    };
  }, []);

  const stale = ageS != null && ageS > 3;

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
                <img src={frameSrc} alt="Live camera frame" className="max-h-full max-w-full object-contain" />
              )}
            </div>
          </Panel>

          <aside className="flex w-[340px] shrink-0 flex-col gap-gutter overflow-y-auto bg-outline-variant">
            <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
              <span className="mb-unit font-label-xs text-label-xs text-primary">PROVENANCE</span>
              <KeyValueRow k="CAMERA SOURCE" v={frame.cameraSource} tone="primary" />
              <KeyValueRow k="ACTUATOR" v={frame.actuatorType} tone="warning" />
              <KeyValueRow k="BACKEND" v={frame.sourceBackend} />
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
          </aside>
        </div>
      )}
    </Screen>
  );
}
