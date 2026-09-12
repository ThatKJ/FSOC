"use client";

import { useEffect, useState } from "react";
import { User } from "lucide-react";

import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { simClock } from "@/lib/format";
import { cn } from "@/lib/cn";
import { ScenarioMenu } from "./ScenarioMenu";
import { SourceToggle } from "./SourceToggle";

/** Fixed header, h-48. Reproduces the Stitch shell header, wired to the sim clock. */
export function TopBar() {
  const { simTime, runState, meta, status } = useSimulation();
  const [mounted, setMounted] = useState(false);
  useEffect(() => setMounted(true), []);

  const uplinkOk = status !== "error";

  return (
    <header className="fixed left-0 right-0 top-0 z-50 flex h-[48px] items-center justify-between border-b border-outline-variant bg-surface-container-lowest px-margin-md">
      <div className="flex shrink-0 items-baseline gap-margin-md">
        <span className="font-display-telem text-headline-sm tracking-widest text-on-surface">
          <span className="lg:hidden">FSOC</span>
          <span className="hidden lg:inline">FSOC ALIGNMENT</span>
        </span>
        <span className="hidden border-l border-outline-variant pl-margin-md font-label-xs text-label-xs uppercase tracking-tight text-on-surface-variant lg:inline">
          SIH26169
        </span>
        <span
          className="border-l border-outline-variant pl-margin-md font-label-xs text-label-xs uppercase tracking-tight text-tertiary"
          title="Every value on this screen comes from the deterministic C++ simulation (SyntheticCameraRenderer) — no physical camera, beacon, or pan/tilt hardware is connected."
        >
          Simulation
        </span>
      </div>

      <div className="flex min-w-0 flex-1 justify-center px-margin-sm">
        <ScenarioMenu />
      </div>

      <div className="flex shrink-0 items-center gap-margin-md">
        <SourceToggle />
        <div className="hidden items-center gap-unit font-data-mono text-data-mono text-on-surface-variant lg:flex">
          <span className="text-primary">SIM</span>
          <span className="tnum">{mounted ? simClock(simTime) : "00.000"}s</span>
        </div>
        <div className="hidden items-center gap-unit border-l border-outline-variant px-margin-sm lg:flex">
          <span
            className={cn(
              "h-2 w-2",
              uplinkOk ? "bg-primary" : "bg-error",
              runState === "RUNNING" && "animate-pulse",
            )}
          />
          <span className="font-label-xs text-label-xs uppercase text-on-surface">
            {uplinkOk ? "Sim Feed Active" : "Sim Feed Fault"}
          </span>
        </div>
        {/* mobile/tablet: the same feed-status dot alone, no label -- still an honest live/fault indicator */}
        <span
          className={cn(
            "h-2 w-2 shrink-0 lg:hidden",
            uplinkOk ? "bg-primary" : "bg-error",
            runState === "RUNNING" && "animate-pulse",
          )}
          title={uplinkOk ? "Sim Feed Active" : "Sim Feed Fault"}
        />
        <div className="ml-margin-sm flex h-8 w-8 shrink-0 items-center justify-center rounded-full bg-primary">
          <User className="h-[18px] w-[18px] text-on-primary" strokeWidth={1.75} />
        </div>
      </div>
    </header>
  );
}
