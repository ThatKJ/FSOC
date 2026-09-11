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
    <Screen className="bg-white">
      <div className="relative flex flex-1 overflow-hidden">
        {/* optical feed */}
        <div className="relative flex-1 overflow-hidden border-r border-gray-200">
          <TrackingFeedLive compact />

          {/* Mini FPA overlay */}
          <div className="absolute left-4 top-4 z-20 rounded-lg bg-white/90 px-3 py-2 font-mono text-sm leading-5 text-apple-blue shadow-sm backdrop-blur-md">
            SENSOR_FPA_ACTIVE
            <br />
            FOV: {fixed(current.camera.horizontalFovDeg, 1)}deg
            <br />
            EXP: 50ms
          </div>

          {/* Bottom status strip */}
          <div className="absolute bottom-0 left-0 right-0 z-30 flex items-end justify-between border-t border-gray-200 bg-white/95 px-4 py-3 backdrop-blur-md">
            <div className="flex gap-6 font-mono text-sm">
              <div className="flex flex-col gap-1">
                <span className="text-xs uppercase text-gray-600">State</span>
                <span className={cn("font-semibold", lost ? "text-status-error" : "text-apple-blue")}>
                  {lost ? "TARGET LOST" : "LOCKED"}
                </span>
              </div>
              <div className="flex flex-col gap-1">
                <span className="text-xs uppercase text-gray-600">Error</span>
                <div className="flex gap-2">
                  <span className="font-medium tabular-nums text-apple-ink">{deg(current.tracking.totalErrorDeg, 3)}</span>
                  <span className="tabular-nums text-gray-500">
                    ({current.tracking.errorXPx != null && current.tracking.errorYPx != null
                      ? px(Math.hypot(current.tracking.errorXPx, current.tracking.errorYPx))
                      : "—"}{" "}
                    px)
                  </span>
                </div>
              </div>
              <div className="flex flex-col gap-1">
                <span className="text-xs uppercase text-gray-600">Attitude</span>
                <div className="flex gap-3 text-gray-600">
                  <span>
                    PAN <span className="font-medium tabular-nums text-apple-ink">{fixed(current.camera.panDeg, 2)}</span>
                  </span>
                  <span>
                    TILT <span className="font-medium tabular-nums text-apple-ink">{fixed(current.camera.tiltDeg, 2)}</span>
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
      <div className="flex h-[120px] shrink-0 border-t border-gray-200 bg-white">
        <div className="relative flex flex-1 flex-col overflow-hidden border-r border-gray-200">
          <PointingErrorChartLive className="flex-1 pt-4" height={84} compact />
        </div>
        <div className="flex w-[320px] shrink-0 flex-col overflow-hidden bg-gray-50 p-4">
          <span className="mb-2 text-xs font-semibold uppercase text-gray-600">
            Event Log
          </span>
          <EventLog className="flex-1" max={12} />
        </div>
      </div>
    </Screen>
  );
}
