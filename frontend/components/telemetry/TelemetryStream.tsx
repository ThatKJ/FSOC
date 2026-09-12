"use client";

import { Button } from "@/components/ui/Button";
import { KeyValueRow } from "@/components/ui/Panel";
import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { deg, elapsedClock, fixed, px, signed } from "@/lib/format";
import { cn } from "@/lib/cn";

/** Mission Control right rail — "Telemetry Stream". All values from the current C++ frame. */
export function TelemetryStream({ className }: { className?: string }) {
  const { current, meta, runState, pause, reset, simTime, playing } = useSimulation();
  const lost = current.trackingState === "TARGET_LOST";
  const t = current.tracking;
  const c = current.camera;

  return (
    <aside
      className={cn(
        "flex w-full shrink-0 flex-col overflow-y-auto border-l-0 border-t border-outline-variant bg-surface-container-low lg:w-[320px] lg:border-l lg:border-t-0",
        className,
      )}
      data-testid="telemetry-stream"
    >
      <div className="flex items-center justify-between border-b border-outline-variant bg-surface-container p-margin-md">
        <span className="font-label-xs text-label-xs uppercase tracking-widest text-on-surface">
          Telemetry Stream
        </span>
        <div className="flex items-center gap-unit">
          <span className={cn("h-1.5 w-1.5", playing ? "animate-pulse bg-primary" : "bg-on-surface-variant")} />
          <span className="font-data-mono text-[10px] text-primary">{playing ? "LIVE" : "HOLD"}</span>
        </div>
      </div>

      <div className="flex flex-col gap-gutter bg-outline-variant">
        <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
          <KeyValueRow k="STATE" v={current.trackingState} tone={lost ? "lost" : "primary"} />
          <KeyValueRow k="CMD MODE" v={meta?.controlEnabled === false ? "OPEN_LOOP" : "AUTO_TRACK"} />
          <KeyValueRow k="RUN STATE" v={runState} border={false} />
        </div>

        <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
          <span className="mb-unit font-label-xs text-label-xs text-primary">ATTITUDE (GIMBAL)</span>
          <KeyValueRow k="PAN [deg]" v={signed(c.panDeg, 4)} />
          <KeyValueRow k="TILT [deg]" v={signed(c.tiltDeg, 4)} />
          <KeyValueRow
            k="RATES [deg/s]"
            border={false}
            v={
              <span className={cn(current.control.panSaturated && "text-tertiary")}>
                {fixed(c.panRateDegS, 3)} / {fixed(c.tiltRateDegS, 3)}
              </span>
            }
          />
        </div>

        <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
          <span className="mb-unit font-label-xs text-label-xs text-tertiary-container">ERROR METRICS</span>
          <KeyValueRow k="TOTAL [deg]" v={deg(t.totalErrorDeg, 4)} tone="warning" />
          <KeyValueRow
            k="PIXEL [px]"
            border={false}
            tone="warning"
            v={
              t.errorXPx != null && t.errorYPx != null
                ? px(Math.hypot(t.errorXPx, t.errorYPx), 2)
                : "—"
            }
          />
        </div>

        {current.perception && (
          <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
            <span className="mb-unit font-label-xs text-label-xs text-on-surface-variant">
              PERCEPTION (STAGE 3)
            </span>
            <KeyValueRow k="MODE" v={current.perception.mode} />
            <KeyValueRow
              k="SOURCE"
              v={current.perception.source}
              tone={current.perception.source === "NONE" ? "lost" : "primary"}
            />
            <KeyValueRow
              k="AI CONF"
              border={current.perception.rejectionReason === "NOT_APPLICABLE"}
              v={
                current.perception.aiPresenceProbability != null
                  ? fixed(current.perception.aiPresenceProbability, 3)
                  : "—"
              }
            />
            {current.perception.rejectionReason !== "NOT_APPLICABLE" && (
              <KeyValueRow k="REJECTED" v={current.perception.rejectionReason} tone="warning" border={false} />
            )}
          </div>
        )}

        {current.tracker && current.tracker.lockState !== "SEARCHING" && (
          <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
            <span className="mb-unit font-label-xs text-label-xs text-on-surface-variant">
              STATE ESTIMATOR (P0-v2)
            </span>
            <KeyValueRow
              k="LOCK STATE"
              v={current.tracker.lockState}
              tone={
                current.tracker.lockState === "TRACKING"
                  ? "primary"
                  : current.tracker.lockState === "COASTING"
                    ? "warning"
                    : "lost"
              }
            />
            <KeyValueRow
              k="CONFIDENCE"
              v={current.tracker.confidence != null ? fixed(current.tracker.confidence, 2) : "—"}
            />
            <KeyValueRow
              k="POSITION"
              v={current.tracker.isPrediction ? "PREDICTED" : "MEASURED"}
              tone={current.tracker.isPrediction ? "warning" : "primary"}
              border={false}
            />
          </div>
        )}

        <div className="flex flex-col gap-margin-sm bg-surface-container-low p-margin-md">
          <span className="mb-unit font-label-xs text-label-xs text-on-surface-variant">TARGET (TRUTH)</span>
          <KeyValueRow k="X / Y / Z [m]" v={`${fixed(current.target.position.x, 1)} / ${fixed(current.target.position.y, 1)} / ${fixed(current.target.position.z, 1)}`} />
          <KeyValueRow k="LOCK TIME" v={elapsedClock(simTime)} border={false} />
        </div>
      </div>

      <div className="mt-auto border-t border-outline-variant bg-surface-container-low p-margin-md">
        <Button
          variant="ghost"
          className="w-full border-primary/50 text-primary hover:bg-primary/10"
          onClick={() => {
            pause();
            reset();
          }}
        >
          Abort Tracking
        </Button>
      </div>
    </aside>
  );
}
