"use client";

import Link from "next/link";
import { useRouter } from "next/navigation";
import { ArrowRight, ChevronRight, Play } from "lucide-react";

import { Screen } from "@/components/shell/AppShell";
import { TrackingFeedLive } from "@/components/tracking/TrackingFeedLive";
import { PointingErrorChartLive } from "@/components/telemetry/PointingErrorChartLive";
import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { CAMERA, SIM_RATE_HZ } from "@/lib/baseline/constants";
import { deg, fixed, px } from "@/lib/format";
import { cn } from "@/lib/cn";

/**
 * / — Stitch "Overview". Hero + live optical viewport with the TARGET → CAMERA →
 * DETECTION → ERROR → CONTROL → GIMBAL pipeline breadcrumb. Buttons are wired.
 */
export default function OverviewPage() {
  const router = useRouter();
  const { current, runScenario, scenario, status } = useSimulation();
  const lost = current.trackingState === "TARGET_LOST";

  return (
    <Screen>
      <div className="flex w-full flex-1 overflow-hidden">
        {/* hero */}
        <div className="relative z-10 flex w-1/3 flex-col justify-center border-r border-outline-variant bg-surface px-12">
          <div className="mb-4">
            <span className="mb-1 block font-label-xs text-label-xs uppercase tracking-widest text-primary">
              SIH26169
            </span>
            <h1 className="font-display-telem text-[36px] font-medium uppercase leading-[40px] tracking-tight text-on-surface">
              FSOC Coarse
              <br />
              Alignment
              <br />
              Control System
            </h1>
          </div>
          <p className="mb-12 max-w-sm border-l-2 border-primary/30 pl-4 font-body-md text-on-surface-variant">
            AI-assisted virtual optical tracking for coarse alignment of mobile free-space
            optical communication terminals. Closed-loop pixel-feedback control under
            actuator constraints — validated C++ engine, frozen at{" "}
            <span className="text-on-surface">v1_baseline</span>.
          </p>

          <div className="flex flex-col gap-4">
            <Link
              href="/mission"
              className="group flex items-center justify-between border border-outline-variant bg-surface-container-highest px-4 py-3 transition-colors duration-200 hover:border-primary hover:bg-primary"
            >
              <span className="font-headline-sm text-headline-sm uppercase tracking-wider text-on-surface group-hover:text-on-primary">
                Enter Mission Control
              </span>
              <ArrowRight className="h-5 w-5 text-primary group-hover:text-on-primary" strokeWidth={1.75} />
            </Link>
            <button
              type="button"
              onClick={() => {
                runScenario(scenario);
                router.push("/tracking");
              }}
              className="group flex items-center justify-between border border-outline-variant px-4 py-3 transition-colors duration-200 hover:bg-surface-container"
            >
              <span className="font-headline-sm text-headline-sm uppercase tracking-wider text-on-surface-variant group-hover:text-on-surface">
                Run Demo Scenario
              </span>
              <Play className="h-5 w-5 text-on-surface-variant group-hover:text-on-surface" fill="currentColor" strokeWidth={2} />
            </button>
          </div>

          <div className="absolute bottom-12 left-12 grid grid-cols-2 gap-x-8 gap-y-4">
            <Stat label="System Status" value={status === "error" ? "LINK FAULT" : "NOMINAL"} tone={status === "error" ? "lost" : "primary"} pulse />
            <Stat label="Uplink Frequency" value="1550 nm" />
            <Stat label="Simulation Rate" value={`${SIM_RATE_HZ}.0 Hz`} />
            <Stat label="Sensor" value={`${CAMERA.widthPx}×${CAMERA.heightPx} · ${CAMERA.horizontalFovDeg}°`} />
          </div>
        </div>

        {/* optical viewport */}
        <div className="relative flex w-2/3 flex-col bg-black">
          <TrackingFeedLive compact label="FSOC OPTICAL TRACKER · OVERVIEW" />

          {/* camera chips */}
          <div className="absolute right-4 top-4 z-20 flex flex-col gap-2">
            {[
              ["FPS", `${SIM_RATE_HZ}.0`, "text-primary"],
              ["EXPOSURE", "15 ms", "text-on-surface"],
              ["GAIN", "12.5 dB", "text-on-surface"],
            ].map(([k, v, c]) => (
              <div
                key={k}
                className="flex w-48 justify-between border border-outline-variant bg-surface-container/80 p-2 font-data-mono text-xs backdrop-blur-sm"
              >
                <span className="text-on-surface-variant">{k}</span>
                <span className={c}>{v}</span>
              </div>
            ))}
          </div>

          {/* bottom status */}
          <div className="z-20 mt-auto flex flex-col border-t border-outline-variant bg-surface-container-highest/90 backdrop-blur-md">
            <div className="flex border-b border-outline-variant">
              <Cell label="State" value={lost ? "TARGET LOST" : "LOCKED"} tone={lost ? "lost" : "primary"} />
              <Cell
                label="Error"
                value={
                  <>
                    {deg(current.tracking.totalErrorDeg, 3)}{" "}
                    <span className="text-on-surface-variant">
                      ({current.tracking.errorXPx != null && current.tracking.errorYPx != null
                        ? px(Math.hypot(current.tracking.errorXPx, current.tracking.errorYPx))
                        : "—"}{" "}
                      px)
                    </span>
                  </>
                }
              />
              <Cell label="Pan / Tilt" value={`${fixed(current.camera.panDeg, 2)} / ${fixed(current.camera.tiltDeg, 2)}`} />
              <div className="relative flex flex-[2] flex-col overflow-hidden p-3">
                <span className="absolute left-3 top-3 z-10 font-label-xs text-label-xs uppercase text-on-surface-variant">
                  Pointing Error (deg)
                </span>
                <PointingErrorChartLive className="mt-4 h-full" height={64} compact hideLabel />
              </div>
            </div>
            <div className="flex gap-2 overflow-x-auto whitespace-nowrap bg-surface-container px-4 py-2 font-data-mono text-xs text-on-surface-variant">
              {[
                ["TARGET", "text-on-surface"],
                ["CAMERA", "text-on-surface"],
                ["DETECTION", "text-primary"],
                ["ERROR", "text-primary"],
                ["CONTROL", "text-tertiary-container"],
                ["GIMBAL", "text-on-surface"],
              ].map(([k, c], i, arr) => (
                <span key={k} className="flex items-center gap-2">
                  <span className={c}>{k}</span>
                  {i < arr.length - 1 && <ChevronRight className="h-3.5 w-3.5" strokeWidth={1.5} />}
                </span>
              ))}
            </div>
          </div>
        </div>
      </div>
    </Screen>
  );
}

function Stat({
  label,
  value,
  tone = "default",
  pulse,
}: {
  label: string;
  value: string;
  tone?: "default" | "primary" | "lost";
  pulse?: boolean;
}) {
  return (
    <div>
      <div className="mb-1 font-label-xs text-label-xs uppercase text-on-surface-variant">{label}</div>
      <div
        className={cn(
          "flex items-center gap-2 font-data-mono",
          tone === "lost" ? "text-error" : tone === "primary" ? "text-primary" : "text-on-surface",
        )}
      >
        {pulse && <span className={cn("h-1.5 w-1.5 rounded-full", tone === "lost" ? "bg-error" : "animate-pulse bg-primary")} />}
        {value}
      </div>
    </div>
  );
}

function Cell({
  label,
  value,
  tone = "default",
}: {
  label: string;
  value: React.ReactNode;
  tone?: "default" | "primary" | "lost";
}) {
  return (
    <div className="flex flex-1 flex-col border-r border-outline-variant p-3">
      <span className="mb-1 font-label-xs text-label-xs uppercase text-on-surface-variant">{label}</span>
      <span
        className={cn(
          "font-data-mono tnum",
          tone === "lost" ? "text-error" : tone === "primary" ? "text-primary" : "text-on-surface",
        )}
      >
        {value}
      </span>
    </div>
  );
}
