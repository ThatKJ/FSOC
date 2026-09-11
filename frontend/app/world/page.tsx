"use client";

import { useState } from "react";
import { Eye, FlipHorizontal, PanelLeft, Video } from "lucide-react";

import { Screen } from "@/components/shell/AppShell";
import { WorldCanvas } from "@/components/world/WorldCanvas";
import type { WorldView } from "@/components/world/WorldScene";
import { MiniTransport } from "@/components/simulation/MiniTransport";
import { KeyValueRow } from "@/components/ui/Panel";
import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { deg, fixed, signed } from "@/lib/format";
import { cn } from "@/lib/cn";

const VIEWS: { id: WorldView; label: string; icon: typeof Eye }[] = [
  { id: "WORLD", label: "WORLD VIEW", icon: Eye },
  { id: "CAMERA", label: "CAMERA", icon: Video },
  { id: "TOP", label: "TOP ORTHO", icon: FlipHorizontal },
  { id: "SIDE", label: "SIDE ELEVATION", icon: PanelLeft },
];

/**
 * /world — Stitch "Spatial View". React Three Fiber scene driven by telemetry:
 * camera terminal (pan/tilt), target marker (target.position), line of sight,
 * FOV frustum, trajectory path, world axes. Not decoration — every transform is
 * a current C++ frame.
 */
export default function WorldPage() {
  const [view, setView] = useState<WorldView>("WORLD");
  const { current } = useSimulation();
  const p = current.target.position;
  const v = current.target.velocity;
  const linSpeed = Math.hypot(v.x, v.y, v.z);
  const lost = current.trackingState === "TARGET_LOST";

  return (
    <Screen className="bg-gray-100">
      <div className="relative flex w-full flex-1">
        <div className="absolute inset-0 z-0">
          <WorldCanvas view={view} />
        </div>

        {/* view switch */}
        <div className="absolute left-6 top-6 z-10 flex flex-col overflow-hidden rounded-lg bg-white/90 shadow-md backdrop-blur-md">
          {VIEWS.map((vw) => {
            const Icon = vw.icon;
            const active = vw.id === view;
            return (
              <button
                key={vw.id}
                onClick={() => setView(vw.id)}
                className={cn(
                  "group flex w-full items-center justify-between px-4 py-2.5 text-left text-xs font-semibold uppercase tracking-wide transition-all",
                  active ? "bg-apple-blue text-white" : "bg-white text-gray-700 hover:bg-gray-100",
                )}
              >
                {vw.label}
                <Icon className={cn("h-4 w-4 transition-opacity", active ? "opacity-100" : "opacity-0 group-hover:opacity-100")} strokeWidth={1.5} />
              </button>
            );
          })}
        </div>

        {/* axes legend */}
        <div className="absolute bottom-6 left-6 z-10 flex gap-3 rounded-lg bg-white/90 px-4 py-2 shadow-md backdrop-blur-md">
          {[
            ["X", "bg-status-error text-status-error"],
            ["Y", "bg-apple-blue text-apple-blue"],
            ["Z", "bg-gray-500 text-gray-700"],
          ].map(([k, c]) => (
            <div key={k} className="flex flex-col items-center gap-1">
              <div className={cn("h-8 w-px", c.split(" ")[0])} />
              <span className={cn("font-mono text-xs font-medium", c.split(" ")[1])}>{k}</span>
            </div>
          ))}
        </div>

        {/* telemetry panel */}
        <aside className="absolute bottom-6 right-6 top-6 z-20 flex w-[320px] flex-col overflow-hidden rounded-lg bg-white/95 shadow-xl backdrop-blur-md">
          <div className="flex items-center justify-between border-b border-gray-200 bg-gray-100/50 px-4 py-3">
            <span className="text-xs font-semibold uppercase tracking-wider text-apple-ink">
              Target Telemetry
            </span>
            <span className={cn("h-2 w-2 rounded-full", lost ? "bg-status-error" : "animate-pulse bg-apple-blue")} />
          </div>

          <div className="flex flex-1 flex-col gap-4 overflow-y-auto p-4">
            <Group label="Position (m)">
              <div className="grid grid-cols-3 gap-1 rounded-lg overflow-hidden">
                <Axis label="X-AXIS" value={fixed(p.x, 2)} color="error" />
                <Axis label="Y-AXIS" value={fixed(p.y, 2)} color="primary" />
                <Axis label="Z-AXIS" value={fixed(p.z, 2)} color="secondary" />
              </div>
            </Group>

            <div className="h-px w-full bg-gray-200" />

            <Group label="Velocity (m/s)">
              <div className="grid grid-cols-2 gap-1 rounded-lg overflow-hidden">
                <Bar label="LINEAR" value={fixed(linSpeed, 2)} pctFill={Math.min(100, linSpeed * 3)} color="primary" />
                <Bar
                  label="|Y RATE|"
                  value={fixed(Math.abs(v.y), 2)}
                  pctFill={Math.min(100, Math.abs(v.y) * 3)}
                  color="secondary"
                />
              </div>
            </Group>

            <div className="h-px w-full bg-gray-200" />

            <Group label="Camera Attitude">
              <div className="flex flex-col rounded-lg bg-gray-50 p-3">
                <KeyValueRow k="PAN" v={`${signed(current.camera.panDeg, 2)}°`} tone="primary" />
                <KeyValueRow k="TILT" v={`${signed(current.camera.tiltDeg, 2)}°`} tone="primary" />
                <KeyValueRow
                  k="RATES [°/s]"
                  border={false}
                  v={`${fixed(current.camera.panRateDegS, 2)} / ${fixed(current.camera.tiltRateDegS, 2)}`}
                />
              </div>
            </Group>

            <div className="relative mt-auto flex flex-col gap-2 overflow-hidden rounded-lg bg-red-50 p-3">
              <div className="absolute left-0 top-0 h-[2px] w-full bg-status-error" />
              <span className="text-xs font-semibold uppercase tracking-wider text-status-error">LOS Error</span>
              <div className="flex items-baseline gap-1">
                <span className="font-mono text-2xl font-semibold tabular-nums text-status-error">
                  {current.tracking.totalErrorDeg != null ? fixed(current.tracking.totalErrorDeg, 3) : "—"}
                </span>
                <span className="font-mono text-sm text-status-error/80">DEG</span>
              </div>
            </div>
          </div>
        </aside>

        {/* transport */}
        <div className="absolute bottom-6 left-1/2 z-20 -translate-x-1/2 rounded-lg bg-white shadow-lg">
          <MiniTransport />
        </div>
      </div>
    </Screen>
  );
}

function Group({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="flex flex-col gap-2">
      <span className="w-fit rounded bg-gray-100 px-2 py-0.5 text-xs font-semibold uppercase tracking-wider text-gray-600">
        {label}
      </span>
      {children}
    </div>
  );
}

function Axis({ label, value, color }: { label: string; value: string; color: "error" | "primary" | "secondary" }) {
  const bar = { error: "bg-status-error/50", primary: "bg-apple-blue/50", secondary: "bg-gray-500/50" }[color];
  return (
    <div className="relative flex flex-col gap-1 overflow-hidden bg-gray-50 p-3">
      <div className={cn("absolute inset-y-0 left-0 w-[2px]", bar)} />
      <span className="text-xs text-gray-600">{label}</span>
      <span className="font-mono text-sm font-medium tabular-nums text-apple-ink">{value}</span>
    </div>
  );
}

function Bar({
  label,
  value,
  pctFill,
  color,
}: {
  label: string;
  value: string;
  pctFill: number;
  color: "primary" | "secondary";
}) {
  const c = color === "primary" ? "bg-apple-blue" : "bg-gray-500";
  const t = color === "primary" ? "text-apple-blue" : "text-gray-700";
  return (
    <div className="flex flex-col gap-1 bg-gray-50 p-3">
      <span className="text-xs text-gray-600">{label}</span>
      <span className={cn("font-mono text-sm font-medium tabular-nums", t)}>{value}</span>
      <div className="relative mt-1 h-[2px] w-full bg-gray-200">
        <div className={cn("absolute inset-y-0 left-0 transition-all duration-300", c)} style={{ width: `${pctFill}%` }} />
      </div>
    </div>
  );
}
