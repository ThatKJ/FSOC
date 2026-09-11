"use client";

import { Cpu, Database } from "lucide-react";
import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { cn } from "@/lib/cn";

/**
 * LOCAL ENGINE MODE  vs  REPLAY MODE toggle.
 * `auto` = prefer the local C++ `fsoc_demo` binary, fall back to the checked-in
 * deterministic replay fixture. The visual components are identical either way.
 */
export function SourceToggle() {
  const { source, setSource, meta } = useSimulation();
  const active = meta?.source; // what actually served the current telemetry

  return (
    <div className="hidden items-center gap-gutter border border-outline-variant bg-surface md:flex">
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
              "flex items-center gap-unit px-margin-sm py-unit font-label-xs text-label-xs uppercase transition-colors",
              selected ? "bg-surface-variant text-primary" : "text-on-surface-variant hover:text-on-surface",
            )}
          >
            <Icon className="h-3 w-3" strokeWidth={1.5} />
            {s}
          </button>
        );
      })}
    </div>
  );
}
