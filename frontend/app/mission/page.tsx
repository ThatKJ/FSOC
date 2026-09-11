"use client";

import { Screen } from "@/components/shell/AppShell";
import { TrackingFeedLive } from "@/components/tracking/TrackingFeedLive";
import { TelemetryStream } from "@/components/telemetry/TelemetryStream";
import { EventLog } from "@/components/simulation/EventLog";
import { PointingErrorChartLive } from "@/components/telemetry/PointingErrorChartLive";
import { MiniTransport } from "@/components/simulation/MiniTransport";
import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { deg, fixed, px } from "@/lib/format";
import { cn } from "@/lib/cn";

/**
 * /mission — Stitch "Mission Control".
 * Optical feed + Telemetry Stream rail on top; pointing-error trace + event log
 * below. Every widget is synchronized to ONE simulation time (useSimulation).
 */
export default function MissionControlPage() {
  const { current } = useSimulation();
  const lost = current.trackingState === "TARGET_LOST";

  return (
    <Screen className="bg-surface-container-lowest">
      <div className="relative flex flex-1 overflow-hidden">
        {/* optical feed */}
        <div className="relative flex-1 overflow-hidden border-r border-outline-variant bg-sensor-black">
          <TrackingFeedLive compact />

          {/* Stitch mini FPA overlay */}
          <div className="absolute left-margin-md top-[52px] z-20 font-data-mono text-data-mono leading-5 text-primary/70">
            SENSOR_FPA_ACTIVE
            <br />
            FOV: {fixed(current.camera.horizontalFovDeg, 1)}deg
            <br />
            EXP: 50ms
          </div>

          {/* Stitch bottom status strip */}
          <div className="absolute bottom-0 left-0 right-0 z-30 flex items-end justify-between border-t border-outline-variant bg-surface-container/90 p-panel-padding backdrop-blur-sm">
            <div className="flex gap-margin-md font-data-mono text-data-mono">
              <div className="flex flex-col gap-unit">
                <span className="font-label-xs text-label-xs uppercase text-on-surface-variant">State</span>
                <span className={cn(lost ? "text-error" : "text-primary-fixed-dim")}>
                  {lost ? "TARGET LOST" : "LOCKED"}
                </span>
              </div>
              <div className="flex flex-col gap-unit">
                <span className="font-label-xs text-label-xs uppercase text-on-surface-variant">Error</span>
                <div className="flex gap-margin-sm">
                  <span className="tnum text-on-surface">{deg(current.tracking.totalErrorDeg, 3)}</span>
                  <span className="tnum text-on-surface-variant">
                    ({current.tracking.errorXPx != null && current.tracking.errorYPx != null
                      ? px(Math.hypot(current.tracking.errorXPx, current.tracking.errorYPx))
                      : "—"}{" "}
                    px)
                  </span>
                </div>
              </div>
              <div className="flex flex-col gap-unit">
                <span className="font-label-xs text-label-xs uppercase text-on-surface-variant">Attitude</span>
                <div className="flex gap-margin-sm text-on-surface-variant">
                  <span>
                    PAN <span className="tnum text-on-surface">{fixed(current.camera.panDeg, 2)}</span>
                  </span>
                  <span>
                    TILT <span className="tnum text-on-surface">{fixed(current.camera.tiltDeg, 2)}</span>
                  </span>
                </div>
              </div>
            </div>
            <MiniTransport className="border-0 bg-transparent p-0 backdrop-blur-0" />
          </div>
        </div>

        <TelemetryStream />
      </div>

      {/* bottom band: pointing error + event log */}
      <div className="flex h-[120px] shrink-0 border-t border-outline-variant bg-surface-container-low">
        <div className="relative flex flex-1 flex-col overflow-hidden border-r border-outline-variant">
          <PointingErrorChartLive className="flex-1 pt-margin-md" height={84} compact />
        </div>
        <div className="flex w-[320px] shrink-0 flex-col overflow-hidden bg-surface-container p-margin-md">
          <span className="mb-margin-sm font-label-xs text-label-xs uppercase text-on-surface-variant">
            Event Log
          </span>
          <EventLog className="flex-1" max={12} />
        </div>
      </div>
    </Screen>
  );
}
