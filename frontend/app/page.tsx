"use client";

import Link from "next/link";
import { useRouter } from "next/navigation";
import { ArrowRight, Play, ChevronRight } from "lucide-react";

import { Screen } from "@/components/shell/AppShell";
import { TrackingFeedLive } from "@/components/tracking/TrackingFeedLive";
import { PointingErrorChartLive } from "@/components/telemetry/PointingErrorChartLive";
import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { CAMERA, SIM_RATE_HZ } from "@/lib/baseline/constants";
import { deg, fixed, px } from "@/lib/format";
import { cn } from "@/lib/cn";

/**
 * Apple-Style Overview — Premium Light Theme
 * Clean, generous whitespace, smooth animations, refined interactions.
 */
export default function OverviewPage() {
  const router = useRouter();
  const { current, runScenario, scenario, status } = useSimulation();
  const lost = current.trackingState === "TARGET_LOST";

  return (
    <Screen className="bg-white">
      {/* Hero Section */}
      <div className="flex min-h-[60vh] flex-col items-center justify-center px-8 py-20 text-center animate-fadeIn">
        <span className="eyebrow mb-4 animate-slideUp" style={{ animationDelay: "100ms" }}>
          SIH26169 · OPTICAL TRACKING
        </span>
        <h1 className="display-lg mb-6 max-w-3xl animate-slideUp" style={{ animationDelay: "200ms" }}>
          FSOC Coarse Alignment Control System
        </h1>
        <p className="subhead mb-10 max-w-2xl animate-slideUp" style={{ animationDelay: "300ms" }}>
          AI-assisted virtual optical tracking for mobile free-space optical communication.
          Closed-loop pixel-feedback control with validated v1_baseline engine.
        </p>

        <div className="flex items-center gap-4 animate-slideUp" style={{ animationDelay: "400ms" }}>
          <Link href="/mission" className="btn-primary">
            Enter Mission Control
            <ArrowRight className="h-4 w-4" strokeWidth={2} />
          </Link>
          <button
            type="button"
            onClick={() => {
              runScenario(scenario);
              router.push("/tracking");
            }}
            className="btn-outline"
          >
            <Play className="h-4 w-4" fill="currentColor" strokeWidth={0} />
            Run Demo
          </button>
        </div>

        {/* Stats Row */}
        <div className="mt-20 flex items-center gap-12 animate-slideUp" style={{ animationDelay: "500ms" }}>
          <Stat label="System Status" value={status === "error" ? "Fault" : "Nominal"} accent={status !== "error"} />
          <Divider />
          <Stat label="Simulation Rate" value={`${SIM_RATE_HZ} Hz`} />
          <Divider />
          <Stat label="Sensor Resolution" value={`${CAMERA.widthPx}×${CAMERA.heightPx}`} />
          <Divider />
          <Stat label="Field of View" value={`${CAMERA.horizontalFovDeg}°`} />
        </div>
      </div>

      {/* Optical Viewport Section - Light with subtle contrast */}
      <div className="relative flex-1 overflow-hidden bg-gray-100 py-12">
        <div className="mx-auto max-w-7xl px-6">
          {/* Section Header */}
          <div className="mb-8 text-center">
            <span className="eyebrow mb-2">Live Optical Tracking</span>
            <h2 className="headline text-apple-ink">FSOC Mission Control</h2>
          </div>

          {/* Viewport Card */}
          <div className="overflow-hidden rounded-2xl bg-white shadow-lg transition-all duration-500 hover:shadow-xl">
            <TrackingFeedLive compact label="FSOC OPTICAL TRACKER" />

            {/* Camera Info - Top Right Overlay */}
            <div className="absolute right-6 top-6 z-10 flex flex-col gap-2 animate-slideUp" style={{ animationDelay: "600ms" }}>
              {[
                ["Frame Rate", `${SIM_RATE_HZ} FPS`],
                ["Exposure", "15 ms"],
                ["Gain", "12.5 dB"],
              ].map(([label, value]) => (
                <div
                  key={label}
                  className="flex w-40 items-center justify-between rounded-lg bg-white/90 px-3 py-2 shadow-sm backdrop-blur-md transition-all duration-300 hover:bg-white hover:shadow-md"
                >
                  <span className="text-xs text-gray-600">{label}</span>
                  <span className="font-mono text-sm font-medium text-apple-ink">{value}</span>
                </div>
              ))}
            </div>

            {/* Telemetry Bar - Bottom */}
            <div className="absolute bottom-0 left-0 right-0 z-10 border-t border-gray-200 bg-white/95 backdrop-blur-xl">
              <div className="flex items-center justify-between px-6 py-4">
                <div className="flex items-center gap-8">
                  <TelemetryCell
                    label="State"
                    value={lost ? "TARGET LOST" : "TRACKING"}
                    accent={!lost}
                    error={lost}
                  />
                  <TelemetryCell
                    label="Error"
                    value={deg(current.tracking.totalErrorDeg, 3)}
                    unit="deg"
                  />
                  <TelemetryCell
                    label="Pan / Tilt"
                    value={`${fixed(current.camera.panDeg, 2)} / ${fixed(current.camera.tiltDeg, 2)}`}
                  />
                </div>

                <div className="flex items-center gap-6">
                  <div className="flex flex-col gap-1">
                    <span className="text-xs text-gray-600">Pointing Error</span>
                    <div className="h-10 w-40 overflow-hidden rounded-lg bg-gray-100">
                      <PointingErrorChartLive height={40} compact hideLabel />
                    </div>
                  </div>
                </div>
              </div>

              {/* Pipeline breadcrumb */}
              <div className="flex items-center gap-2 border-t border-gray-200 px-6 py-2.5 text-xs">
                {["Target", "Camera", "Detection", "Error", "Control", "Gimbal"].map((step, i, arr) => (
                  <span key={step} className="flex items-center gap-2 transition-all duration-300">
                    <span
                      className={cn(
                        "transition-all duration-300",
                        i === 2 || i === 3 ? "text-apple-blue font-medium" : "text-gray-500"
                      )}
                    >
                      {step}
                    </span>
                    {i < arr.length - 1 && <ChevronRight className="h-3 w-3 text-gray-400" strokeWidth={1.5} />}
                  </span>
                ))}
              </div>
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
  accent,
}: {
  label: string;
  value: string;
  accent?: boolean;
}) {
  return (
    <div className="flex flex-col items-center gap-2 transition-all duration-500 hover:scale-105">
      <span className="caption">{label}</span>
      <div className="flex items-center gap-2">
        {accent !== undefined && (
          <span className={cn("status-dot transition-all duration-300", accent ? "primary active" : "error")} />
        )}
        <span className="data-value transition-all duration-300">{value}</span>
      </div>
    </div>
  );
}

function Divider() {
  return <div className="h-8 w-px bg-gray-300 transition-all duration-300" />;
}

function TelemetryCell({
  label,
  value,
  unit,
  accent,
  error,
}: {
  label: string;
  value: string;
  unit?: string;
  accent?: boolean;
  error?: boolean;
}) {
  return (
    <div className="flex flex-col gap-1 transition-all duration-300">
      <span className="text-xs text-gray-600">{label}</span>
      <span
        className={cn(
          "font-mono text-lg font-semibold transition-all duration-300",
          error ? "text-status-error" : accent ? "text-apple-blue" : "text-apple-ink"
        )}
      >
        {value}
        {unit && <span className="ml-1 text-sm text-gray-500">{unit}</span>}
      </span>
    </div>
  );
}
