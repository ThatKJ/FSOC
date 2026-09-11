"use client";

import { Cpu, Database } from "lucide-react";
import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { cn } from "@/lib/cn";

/**
 * SpaceX-style source toggle — minimal, ghost borders.
 */
export function SourceToggle() {
  const { source, setSource, meta } = useSimulation();
  const active = meta?.source;

  return (
    <div className="hidden items-center border border-ghost-border md:flex">
      {(["engine", "replay"] as const).map((s) => {
        const Icon = s === "engine" ? Cpu : Database;
        const selected = source === s || (source === "auto" && active === s);
        return (
          <button
            key={s}
            type="button"
            onClick={() => setSource(source === s ? "auto" : s)}
            title={
              s === "engine"
                ? "LOCAL ENGINE — run the real fsoc_demo binary"
                : "REPLAY — checked-in deterministic C++ telemetry"
            }
            className={cn(
              "flex items-center gap-2 px-3 py-1.5 text-[10px] tracking-[0.12em] transition-all duration-300",
              selected
                ? "bg-ghost text-spectral"
                : "text-spectral-muted hover:bg-ghost hover:text-spectral",
            )}
          >
            <Icon className="h-3 w-3" strokeWidth={1.5} />
            {s.toUpperCase()}
          </button>
        );
      })}
    </div>
  );
}
