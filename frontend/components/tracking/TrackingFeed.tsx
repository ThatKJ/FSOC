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
  errorSeries?: number[];
  videoSrc?: string;
  useVideo?: boolean;
  showHud?: boolean;
  compact?: boolean;
  label?: string;
  className?: string;
}

/**
 * Apple-style optical viewport — sensor-black background,
 * minimal HUD overlays, refined reticle and indicators.
 */
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
  const boxSize = compact ? 44 : 56;
  const ringSize = compact ? 64 : 80;
  const sparkPoints = polyline(errorSeries.length ? errorSeries : [0, 0], 100, 30);

  return (
    <div
      ref={ref}
      className={cn("relative h-full w-full overflow-hidden bg-sensor-black transition-all duration-500", className)}
      data-testid="tracking-feed"
      data-tracking-state={snapshot.trackingState}
    >
      {/* Optical layer */}
      {useVideo && videoSrc && videoOk ? (
        <video
          ref={videoRef}
          className="absolute inset-0 h-full w-full object-cover opacity-80"
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

      {/* Subtle grain */}
      <div className="grain pointer-events-none absolute inset-0" />

      {/* Crosshairs */}
      <svg
        className="pointer-events-none absolute inset-0 h-full w-full"
        width={width || 1}
        height={height || 1}
      >
        {/* Horizontal line */}
        <line
          x1={rect.x}
          x2={rect.x + rect.w}
          y1={rect.cy}
          y2={rect.cy}
          stroke={lost ? "#ff3b30" : "rgba(240, 240, 250, 0.15)"}
          strokeWidth={1}
        />
        {/* Vertical line */}
        <line
          x1={rect.cx}
          x2={rect.cx}
          y1={rect.y}
          y2={rect.y + rect.h}
          stroke={lost ? "#ff3b30" : "rgba(240, 240, 250, 0.15)"}
          strokeWidth={1}
        />
        {/* Center point */}
        <circle
          cx={rect.cx}
          cy={rect.cy}
          r={4}
          fill="none"
          stroke={lost ? "#ff3b30" : "#f0f0fa"}
          strokeWidth={1}
          opacity={lost ? 0.5 : 0.6}
        />

        {/* Error vector */}
        {reticle && (
          <line
            x1={rect.cx}
            y1={rect.cy}
            x2={reticle.x}
            y2={reticle.y}
            stroke="#ff9500"
            strokeWidth={1}
            strokeDasharray="4 4"
            opacity={0.7}
          />
        )}
      </svg>

      {/* Reticle */}
      {reticle && (
        <>
          <div
            className="absolute z-10 border border-spectral"
            style={{
              left: reticle.x,
              top: reticle.y,
              width: boxSize,
              height: boxSize,
              transform: "translate(-50%, -50%)",
            }}
            data-testid="detection-reticle"
          >
            {/* Corner brackets */}
            <span className="absolute -left-px -top-px h-3 w-1 bg-spectral" />
            <span className="absolute -left-px -top-px h-1 w-3 bg-spectral" />
            <span className="absolute -right-px -top-px h-3 w-1 bg-spectral" />
            <span className="absolute -right-px -top-px h-1 w-3 bg-spectral" />
            <span className="absolute -bottom-px -left-px h-3 w-1 bg-spectral" />
            <span className="absolute -bottom-px -left-px h-1 w-3 bg-spectral" />
            <span className="absolute -bottom-px -right-px h-3 w-1 bg-spectral" />
            <span className="absolute -bottom-px -right-px h-1 w-3 bg-spectral" />
          </div>
          {/* Outer ring */}
          <div
            className="absolute z-10 rounded-full border border-status-warning/60"
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

      {/* TARGET LOST */}
      {lost && (
        <div className="absolute inset-0 z-20 flex items-center justify-center" data-testid="target-lost">
          <div className="border border-status-error bg-space-black/80 px-8 py-4">
            <span className="text-2xl font-bold tracking-[0.1em] text-status-error">
              TARGET LOST
            </span>
          </div>
        </div>
      )}

      {/* Top-left label */}
      {label && (
        <div className="pointer-events-none absolute left-4 top-4 z-20">
          <div className="flex items-center gap-2">
            <span className={`h-1.5 w-1.5 rounded-full ${lost ? "bg-status-error" : "bg-spectral animate-pulse"}`} />
            <span className="text-xs tracking-[0.15em] text-spectral">{label}</span>
          </div>
          <div className="mt-1 flex items-center gap-3 text-xs tracking-[0.1em] text-spectral-muted">
            <span>CAM-01</span>
            <span className="opacity-30">|</span>
            <span>{CAMERA.widthPx}×{CAMERA.heightPx}</span>
            <span className="opacity-30">|</span>
            <span>{SIM_RATE_HZ} FPS</span>
          </div>
        </div>
      )}

      {/* Rate limit indicator */}
      {saturated && (
        <div className="absolute right-4 top-4 z-20 border border-status-warning bg-space-black/80 px-3 py-1">
          <span className="text-xs tracking-[0.1em] text-status-warning">RATE LIMIT</span>
        </div>
      )}

      {/* Bottom HUD */}
      {showHud && (
        <div className="absolute bottom-0 left-0 z-20 flex w-full items-end justify-between border-t border-ghost-border bg-space-black/80 p-4 backdrop-blur-sm">
          <div className="flex flex-col gap-2">
            <div className="flex items-baseline gap-4">
              <span className="w-12 text-xs tracking-[0.1em] text-spectral-muted">STATE</span>
              <span className={`text-sm font-bold tracking-[0.08em] ${lost ? "text-status-error" : "text-spectral"}`}>
                {snapshot.trackingState}
              </span>
            </div>
            <div className="flex items-baseline gap-4">
              <span className="w-12 text-xs tracking-[0.1em] text-spectral-muted">ERROR</span>
              <span className="font-mono text-sm text-spectral tnum">
                {deg(snapshot.tracking.totalErrorDeg, 3)}
                <span className="ml-2 text-spectral-muted">
                  ({snapshot.tracking.errorXPx != null && snapshot.tracking.errorYPx != null
                    ? px(Math.hypot(snapshot.tracking.errorXPx, snapshot.tracking.errorYPx))
                    : "—"} px)
                </span>
              </span>
            </div>
            <div className="flex items-baseline gap-4">
              <span className="w-12 text-xs tracking-[0.1em] text-spectral-muted">GIMBAL</span>
              <span className="font-mono text-sm text-spectral tnum">
                P {fixed(snapshot.camera.panDeg, 2)} / T {fixed(snapshot.camera.tiltDeg, 2)}
              </span>
              <span className="ml-4 font-mono text-sm text-spectral-muted tnum">
                t = {simClock(snapshot.simulationTime)}s
              </span>
            </div>
          </div>

          {/* Sparkline */}
          <div className="flex flex-col gap-1">
            <span className="text-right text-xs tracking-[0.1em] text-spectral-muted">POINTING ERROR</span>
            <div className="h-12 w-48 border border-ghost-border bg-space-black">
              <svg className="h-full w-full" preserveAspectRatio="none" viewBox="0 0 100 30">
                <polyline
                  fill="none"
                  points={sparkPoints}
                  stroke="#ff9500"
                  strokeWidth={1}
                />
              </svg>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
