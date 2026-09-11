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
    <Screen className="bg-gray-100">
      <div className="flex flex-1 flex-col">
        <div className="mx-auto w-full max-w-7xl px-6 py-6">
          <div className="mb-4">
            <h2 className="headline text-apple-ink">Live Optical Tracking</h2>
            <p className="body-sm mt-1">Real-time sensor feed with closed-loop control</p>
          </div>
        </div>

        <div className="mx-auto w-full max-w-7xl flex-1 px-6 pb-6">
          <div className="relative h-full overflow-hidden rounded-2xl bg-white shadow-lg">
            <TrackingFeedLive showHud />

            {status === "error" && (
              <div className="absolute left-1/2 top-1/2 z-30 -translate-x-1/2 -translate-y-1/2 rounded-lg border-2 border-status-error bg-white p-6 text-center shadow-xl">
                <div className="text-lg font-semibold uppercase text-status-error">Telemetry Link Fault</div>
                <div className="mt-2 max-w-md font-mono text-sm text-gray-600">{error}</div>
              </div>
            )}
          </div>
        </div>
      </div>
      <div className="border-t border-gray-200 bg-white">
        <PlaybackControls />
      </div>
    </Screen>
  );
}
