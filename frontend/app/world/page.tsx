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
    <Screen className="bg-surface-container-lowest">
      <div className="relative flex w-full flex-1">
        <div className="absolute inset-0 z-0">
          <WorldCanvas view={view} />
        </div>

        {/* view switch */}
        <div className="absolute left-margin-md top-margin-md z-10 flex flex-col gap-gutter bg-surface-container/80 p-unit shadow-md backdrop-blur-md">
          {VIEWS.map((vw) => {
            const Icon = vw.icon;
            const active = vw.id === view;
            return (
              <button
                key={vw.id}
                onClick={() => setView(vw.id)}
                className={cn(
                  "group flex w-full items-center justify-between px-margin-md py-margin-sm text-left font-headline-sm text-headline-sm transition-colors",
                  active ? "bg-surface-container-highest text-primary" : "bg-surface-container-low text-on-surface-variant hover:bg-surface-bright",
                )}
              >
                {vw.label}
                <Icon className={cn("h-4 w-4 transition-opacity", active ? "opacity-100" : "opacity-0 group-hover:opacity-100")} strokeWidth={1.5} />
              </button>
            );
          })}
        </div>

        {/* axes legend */}
        <div className="absolute bottom-margin-md left-margin-md z-10 flex gap-margin-sm">
          {[
            ["X", "bg-error text-error"],
            ["Y", "bg-primary text-primary"],
            ["Z", "bg-secondary text-secondary"],
          ].map(([k, c]) => (
            <div key={k} className="flex flex-col items-center gap-unit">
              <div className={cn("h-8 w-px", c.split(" ")[0])} />
              <span className={cn("font-data-mono text-label-xs", c.split(" ")[1])}>{k}</span>
            </div>
          ))}
        </div>

        {/* telemetry panel */}
        <aside className="absolute bottom-margin-md right-margin-md top-margin-md z-20 flex w-[320px] flex-col overflow-hidden bg-surface-container/90 shadow-xl backdrop-blur-md">
          <div className="flex items-center justify-between bg-surface-container-highest p-panel-padding">
            <span className="font-headline-sm text-headline-sm uppercase tracking-widest text-on-surface">
              Target Telemetry
            </span>
            <span className={cn("h-2 w-2 rounded-full", lost ? "bg-error" : "animate-pulse bg-primary")} />
          </div>

          <div className="flex flex-1 flex-col gap-margin-md overflow-y-auto p-panel-padding">
            <Group label="Position (m)">
              <div className="grid grid-cols-3 gap-gutter bg-outline-variant">
                <Axis label="X-AXIS" value={fixed(p.x, 2)} color="error" />
                <Axis label="Y-AXIS" value={fixed(p.y, 2)} color="primary" />
                <Axis label="Z-AXIS" value={fixed(p.z, 2)} color="secondary" />
              </div>
            </Group>

            <div className="h-px w-full bg-gradient-to-r from-outline-variant to-transparent" />

            <Group label="Velocity (m/s)">
              <div className="grid grid-cols-2 gap-gutter bg-outline-variant">
                <Bar label="LINEAR" value={fixed(linSpeed, 2)} pctFill={Math.min(100, linSpeed * 3)} color="primary" />
                <Bar
                  label="|Y RATE|"
                  value={fixed(Math.abs(v.y), 2)}
                  pctFill={Math.min(100, Math.abs(v.y) * 3)}
                  color="secondary"
                />
              </div>
            </Group>

            <div className="h-px w-full bg-gradient-to-r from-outline-variant to-transparent" />

            <Group label="Camera Attitude">
              <div className="flex flex-col bg-surface-container-low p-margin-sm">
                <KeyValueRow k="PAN" v={`${signed(current.camera.panDeg, 2)}°`} tone="primary" />
                <KeyValueRow k="TILT" v={`${signed(current.camera.tiltDeg, 2)}°`} tone="primary" />
                <KeyValueRow
                  k="RATES [°/s]"
                  border={false}
                  v={`${fixed(current.camera.panRateDegS, 2)} / ${fixed(current.camera.tiltRateDegS, 2)}`}
                />
              </div>
            </Group>

            <div className="relative mt-auto flex flex-col gap-margin-sm overflow-hidden bg-error/10 p-margin-sm shadow-inner">
              <div className="absolute left-0 top-0 h-[2px] w-full bg-error" />
              <span className="font-label-xs text-label-xs uppercase tracking-widest text-error">LOS Error</span>
              <div className="flex items-baseline gap-unit">
                <span className="font-display-telem text-display-telem tnum text-error">
                  {current.tracking.totalErrorDeg != null ? fixed(current.tracking.totalErrorDeg, 3) : "—"}
                </span>
                <span className="font-data-mono text-data-mono text-error/80">DEG</span>
              </div>
            </div>
          </div>
        </aside>

        {/* transport */}
        <div className="absolute bottom-margin-md left-1/2 z-20 -translate-x-1/2">
          <MiniTransport />
        </div>
      </div>
    </Screen>
  );
}

function Group({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="flex flex-col gap-margin-sm">
      <span className="w-fit bg-surface px-unit py-[2px] font-label-xs text-label-xs uppercase tracking-widest text-on-surface-variant">
        {label}
      </span>
      {children}
    </div>
  );
}

function Axis({ label, value, color }: { label: string; value: string; color: "error" | "primary" | "secondary" }) {
  const bar = { error: "bg-error/50", primary: "bg-primary/50", secondary: "bg-secondary/50" }[color];
  return (
    <div className="relative flex flex-col gap-unit overflow-hidden bg-surface-container-low p-margin-sm">
      <div className={cn("absolute inset-y-0 left-0 w-[2px]", bar)} />
      <span className="font-label-xs text-label-xs text-on-surface-variant">{label}</span>
      <span className="font-data-mono text-data-mono tnum text-on-surface">{value}</span>
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
  const c = color === "primary" ? "bg-primary" : "bg-secondary";
  const t = color === "primary" ? "text-primary" : "text-secondary";
  return (
    <div className="flex flex-col gap-unit bg-surface-container-low p-margin-sm">
      <span className="font-label-xs text-label-xs text-on-surface-variant">{label}</span>
      <span className={cn("font-data-mono text-data-mono tnum", t)}>{value}</span>
      <div className="relative mt-unit h-[2px] w-full bg-surface">
        <div className={cn("absolute inset-y-0 left-0", c)} style={{ width: `${pctFill}%` }} />
      </div>
    </div>
  );
}
