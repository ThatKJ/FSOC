"use client";

import { useMemo, useRef, useState } from "react";
import { cn } from "@/lib/cn";
import { CAMERA, SIM_RATE_HZ } from "@/lib/baseline/constants";
import type { DemoSnapshot } from "@/lib/telemetry/types";
import { useElementSize } from "@/lib/useElementSize";
import { placeReticle, polyline, sensorRect } from "@/lib/tracking/viewport";
import { deg, px, simClock, fixed } from "@/lib/format";
import { Starfield } from "./Starfield";

export interface TrackingFeedProps {
  snapshot: DemoSnapshot;
  /** trailing total-angular-error series (deg) for the embedded sparkline */
  errorSeries?: number[];
  /**
   * Optional optical camera video (loops behind the overlays). Off by default —
   * the elegant starfield fallback is the standard viewport. A clean (un-annotated)
   * capture can be dropped at frontend/public/demo/fsoc-tracking.mp4 and enabled
   * with `useVideo`. The Step-9 visualizer MP4s carry their own HUD so they are
   * NOT used here.
   */
  videoSrc?: string;
  useVideo?: boolean;
  /** show the bottom telemetry HUD (Optical Tracking screen) */
  showHud?: boolean;
  /** smaller reticle + minimal chrome (Mission Control inset) */
  compact?: boolean;
  label?: string;
  className?: string;
}

export function TrackingFeed({
  snapshot,
  errorSeries = [],
  videoSrc,
  useVideo = false,
  showHud = false,
  compact = false,
  label = "FSOC OPTICAL TRACKER",
  className,
}: TrackingFeedProps) {
  const { ref, width, height } = useElementSize<HTMLDivElement>();
  const [videoOk, setVideoOk] = useState(true);
  const videoRef = useRef<HTMLVideoElement>(null);

  const rect = useMemo(() => sensorRect(width || 1, height || 1), [width, height]);
  const reticle = useMemo(() => placeReticle(snapshot, rect), [snapshot, rect]);

  const lost = snapshot.trackingState === "TARGET_LOST";
  const saturated = snapshot.control.panSaturated || snapshot.control.tiltSaturated;
  const boxSize = compact ? 40 : 48;
  const ringSize = compact ? 56 : 64;
  const sparkPoints = polyline(errorSeries.length ? errorSeries : [0, 0], 100, 30);

  return (
    <div
      ref={ref}
      className={cn(
        "relative h-full w-full overflow-hidden bg-sensor-black",
        className,
      )}
      data-testid="tracking-feed"
      data-tracking-state={snapshot.trackingState}
    >
      {/* optical layer: clean starfield (default) or an opt-in un-annotated video */}
      {useVideo && videoSrc && videoOk ? (
        <video
          ref={videoRef}
          className="absolute inset-0 h-full w-full object-cover opacity-90"
          src={videoSrc}
          autoPlay
          loop
          muted
          playsInline
          onError={() => setVideoOk(false)}
        />
      ) : (
        <Starfield />
      )}

      {/* sensor grain */}
      <div className="grain pointer-events-none absolute inset-0 opacity-[0.12] mix-blend-overlay" />

      {/* overlay */}
      <svg
        className="pointer-events-none absolute inset-0 h-full w-full"
        width={width || 1}
        height={height || 1}
      >
        {/* crosshair through the optical centre */}
        <line
          x1={rect.x}
          x2={rect.x + rect.w}
          y1={rect.cy}
          y2={rect.cy}
          stroke={lost ? "var(--lost)" : "var(--border)"}
          strokeOpacity={lost ? 0.5 : 0.35}
          strokeWidth={1}
        />
        <line
          x1={rect.cx}
          x2={rect.cx}
          y1={rect.y}
          y2={rect.y + rect.h}
          stroke={lost ? "var(--lost)" : "var(--border)"}
          strokeOpacity={lost ? 0.5 : 0.35}
          strokeWidth={1}
        />
        {/* centre gap tick */}
        <circle cx={rect.cx} cy={rect.cy} r={compact ? 2 : 3} fill="none" stroke="var(--tracking)" strokeWidth={1} strokeOpacity={lost ? 0.2 : 0.7} />

        {/* error vector: optical centre -> detected centroid */}
        {reticle && (
          <line
            x1={rect.cx}
            y1={rect.cy}
            x2={reticle.x}
            y2={reticle.y}
            stroke="var(--warning)"
            strokeWidth={1}
            strokeDasharray="3 3"
            strokeOpacity={0.85}
          />
        )}
      </svg>

      {/* detected reticle + lock ring */}
      {reticle && (
        <>
          <div
            className="absolute z-10 border border-primary"
            style={{
              left: reticle.x,
              top: reticle.y,
              width: boxSize,
              height: boxSize,
              transform: "translate(-50%, -50%)",
            }}
            data-testid="detection-reticle"
          >
            <span className="absolute -left-px -top-px h-2 w-[6px] bg-primary" />
            <span className="absolute -left-px -top-px h-[6px] w-px bg-primary" />
            <span className="absolute -right-px -top-px h-2 w-[6px] bg-primary" />
            <span className="absolute -right-px -top-px h-[6px] w-px bg-primary" />
            <span className="absolute -bottom-px -left-px h-2 w-[6px] bg-primary" />
            <span className="absolute -bottom-px -left-px h-[6px] w-px bg-primary" />
            <span className="absolute -bottom-px -right-px h-2 w-[6px] bg-primary" />
            <span className="absolute -bottom-px -right-px h-[6px] w-px bg-primary" />
          </div>
          <div
            className="absolute z-10 rounded-full border border-tertiary opacity-70"
            style={{
              left: reticle.x,
              top: reticle.y,
              width: ringSize,
              height: ringSize,
              transform: "translate(-50%, -50%)",
            }}
          />
        </>
      )}

      {/* target-lost banner */}
      {lost && (
        <div className="absolute inset-0 z-20 flex items-center justify-center" data-testid="target-lost">
          <div className="border border-error bg-error-container/20 px-margin-md py-margin-sm backdrop-blur-sm">
            <span className="font-display-telem text-display-telem uppercase tracking-widest text-error">
              TARGET LOST
            </span>
          </div>
        </div>
      )}

      {/* top-left instrumentation */}
      <div className="pointer-events-none absolute left-margin-md top-margin-md z-20 flex flex-col gap-unit">
        <div className="flex items-center gap-margin-sm">
          <span className={cn("h-1.5 w-1.5", lost ? "bg-error" : "animate-pulse bg-primary")} />
          <span className="font-data-mono text-label-xs tracking-widest text-primary">{label}</span>
        </div>
        <div className="flex items-center gap-margin-md font-data-mono text-label-xs text-on-surface-variant">
          <span>CAM-01</span>
          <span className="h-3 w-px bg-outline-variant" />
          <span>
            {CAMERA.widthPx}×{CAMERA.heightPx}
          </span>
          <span className="h-3 w-px bg-outline-variant" />
          <span>{SIM_RATE_HZ} Hz</span>
        </div>
      </div>

      {/* saturation indicator */}
      {saturated && (
        <div className="absolute right-margin-md top-margin-md z-20 border border-tertiary bg-surface-container-highest px-margin-sm py-[2px]">
          <span className="font-label-xs text-label-xs uppercase tracking-widest text-tertiary">
            Rate Limit
          </span>
        </div>
      )}

      {/* bottom HUD */}
      {showHud && (
        <div className="absolute bottom-0 left-0 z-20 flex w-full items-end justify-between border-t border-outline-variant/30 bg-surface-container-lowest/80 p-margin-md backdrop-blur-sm">
          <div className="flex flex-col gap-unit">
            <div className="flex items-baseline gap-margin-md">
              <span className="w-12 font-data-mono text-label-xs text-on-surface-variant">STATE</span>
              <span
                className={cn(
                  "font-data-mono text-data-mono",
                  lost ? "text-error" : "text-primary",
                )}
                data-testid="hud-state"
              >
                {snapshot.trackingState}
              </span>
            </div>
            <div className="flex items-baseline gap-margin-md">
              <span className="w-12 font-data-mono text-label-xs text-on-surface-variant">ERROR</span>
              <div className="flex gap-margin-sm font-data-mono text-data-mono text-on-surface">
                <span className="tnum">{deg(snapshot.tracking.totalErrorDeg, 3)}</span>
                <span className="text-on-surface-variant">
                  ({" "}
                  <span className="tnum">
                    {snapshot.tracking.errorXPx != null && snapshot.tracking.errorYPx != null
                      ? px(Math.hypot(snapshot.tracking.errorXPx, snapshot.tracking.errorYPx))
                      : "—"}
                  </span>{" "}
                  px )
                </span>
              </div>
            </div>
            <div className="mt-unit flex items-baseline gap-margin-md">
              <div className="flex gap-margin-sm font-data-mono text-label-xs text-on-surface-variant">
                <span>PAN</span>
                <span className="tnum text-on-surface">{fixed(snapshot.camera.panDeg, 2)}</span>
                <span className="ml-margin-sm">TILT</span>
                <span className="tnum text-on-surface">{fixed(snapshot.camera.tiltDeg, 2)}</span>
                <span className="ml-margin-sm">t =</span>
                <span className="tnum text-on-surface">{simClock(snapshot.simulationTime)}s</span>
              </div>
            </div>
          </div>

          <div className="relative h-16 w-64 border border-outline-variant bg-surface-container-low">
            <span className="absolute left-margin-sm top-unit z-10 font-data-mono text-label-xs text-on-surface-variant">
              POINTING ERROR
            </span>
            <svg
              className="absolute inset-0 h-full w-full"
              preserveAspectRatio="none"
              viewBox="0 0 100 30"
            >
              <polyline
                fill="none"
                points={sparkPoints}
                stroke="var(--warning)"
                strokeWidth={0.6}
              />
            </svg>
          </div>
        </div>
      )}
    </div>
  );
}
