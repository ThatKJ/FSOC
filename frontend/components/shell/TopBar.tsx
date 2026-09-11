"use client";

import { useEffect, useState } from "react";
import { User } from "lucide-react";

import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { simClock } from "@/lib/format";
import { cn } from "@/lib/cn";
import { ScenarioMenu } from "./ScenarioMenu";
import { SourceToggle } from "./SourceToggle";

/**
 * Apple-style header — clean, minimal, light with subtle borders.
 */
export function TopBar() {
  const { simTime, runState, meta, status } = useSimulation();
  const [mounted, setMounted] = useState(false);
  useEffect(() => setMounted(true), []);

  const active = status !== "error";

  return (
    <header className="fixed left-0 right-0 top-0 z-50 flex h-12 items-center justify-between border-b border-gray-300 bg-white/80 px-4 backdrop-blur-xl">
      {/* Left: Brand */}
      <div className="flex items-center gap-4">
        <div className="flex items-center gap-2">
          <span className={cn("status-dot", runState === "RUNNING" && "active", active ? "primary" : "error")} />
          <span className="text-base font-semibold text-apple-ink">FSOC</span>
        </div>

        <span className="h-4 w-px bg-gray-300" />

        <span className="text-sm text-gray-500">Alignment System</span>

        <span className="h-4 w-px bg-gray-300" />

        <span className="rounded-full bg-amber-100 px-2 py-0.5 text-xs font-medium text-amber-700">
          Simulation
        </span>
      </div>

      {/* Center: Scenario */}
      <div className="absolute left-1/2 -translate-x-1/2">
        <ScenarioMenu />
      </div>

      {/* Right: Status */}
      <div className="flex items-center gap-4">
        <SourceToggle />

        <span className="h-4 w-px bg-gray-300" />

        {/* Clock */}
        <div className="flex items-center gap-1.5">
          <span className="text-xs text-gray-500">T+</span>
          <span className="font-mono text-sm font-medium text-apple-ink">
            {mounted ? simClock(simTime) : "00.000"}s
          </span>
        </div>

        <span className="h-4 w-px bg-gray-300" />

        {/* Status */}
        <div className="flex items-center gap-2">
          <span className={cn("status-dot", active ? "primary active" : "error")} />
          <span className="text-sm text-gray-600">
            {active ? "Active" : "Fault"}
          </span>
        </div>

        {/* Avatar */}
        <div className="flex h-8 w-8 items-center justify-center rounded-full bg-apple-blue text-white shadow-sm">
          <User className="h-4 w-4" strokeWidth={2} />
        </div>
      </div>
    </header>
  );
}
