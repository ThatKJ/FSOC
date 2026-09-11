"use client";

import { Screen } from "@/components/shell/AppShell";
import { TrackingFeedLive } from "@/components/tracking/TrackingFeedLive";
import { PlaybackControls } from "@/components/simulation/PlaybackControls";
import { useSimulation } from "@/lib/simulation/SimulationProvider";

/**
 * /tracking — Stitch "Optical Tracking".
 * Full-bleed optical feed: centre crosshair, detected reticle driven by
 * detection.xPx/yPx, centre->detection error vector, TARGET LOST handling,
 * bottom telemetry HUD + pointing-error sparkline. The reticle disappears on
 * loss and returns on natural reacquisition — all from the C++ frames.
 * (Stitch shows a static viewport; a transport bar is added — playback must work.)
 */
export default function TrackingPage() {
  const { status, error } = useSimulation();

  return (
    <Screen className="bg-sensor-black">
      <div className="relative flex-1 overflow-hidden">
        <TrackingFeedLive showHud />

        {status === "error" && (
          <div className="absolute left-1/2 top-1/2 z-30 -translate-x-1/2 -translate-y-1/2 border border-error bg-error-container/20 p-margin-md text-center">
            <div className="font-headline-sm text-headline-sm uppercase text-error">Telemetry Link Fault</div>
            <div className="mt-1 max-w-md font-data-mono text-label-xs text-on-surface-variant">{error}</div>
          </div>
        )}
      </div>
      <PlaybackControls />
    </Screen>
  );
}
