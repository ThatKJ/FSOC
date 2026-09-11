"use client";

import { useEffect, useRef, useState } from "react";
import { ChevronDown, Check } from "lucide-react";

import { SCENARIOS, SCENARIO_IDS, type ScenarioId } from "@/lib/baseline/constants";
import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { cn } from "@/lib/cn";

/**
 * The header "current scenario" pill (Stitch: bg-surface-container border pill with
 * a pulsing primary dot). Made functional: click to switch the active DemoScenario.
 */
export function ScenarioMenu() {
  const { scenario, setScenario, meta, status } = useSimulation();
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!open) return;
    const onDoc = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(false);
    };
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && setOpen(false);
    document.addEventListener("mousedown", onDoc);
    document.addEventListener("keydown", onKey);
    return () => {
      document.removeEventListener("mousedown", onDoc);
      document.removeEventListener("keydown", onKey);
    };
  }, [open]);

  const label = SCENARIOS[scenario].label;

  return (
    <div ref={ref} className="relative">
      <button
        type="button"
        aria-haspopup="listbox"
        aria-expanded={open}
        aria-label={`Active scenario: ${label}. Change scenario`}
        onClick={() => setOpen((v) => !v)}
        className="flex items-center gap-margin-sm border border-outline-variant bg-surface-container px-margin-md py-unit transition-colors hover:border-primary/60"
      >
        <span
          className={cn(
            "h-2 w-2 bg-primary",
            status === "loading" ? "animate-pulse" : status === "error" && "bg-error",
          )}
        />
        <span className="font-data-mono text-data-mono uppercase text-primary">{label}</span>
        <ChevronDown className="h-3 w-3 text-on-surface-variant" strokeWidth={1.5} />
      </button>

      {open && (
        <ul
          role="listbox"
          className="absolute left-1/2 top-[calc(100%+6px)] z-50 w-[240px] -translate-x-1/2 border border-outline-variant bg-surface-container-low shadow-xl"
        >
          {SCENARIO_IDS.map((id: ScenarioId) => {
            const s = SCENARIOS[id];
            const activeItem = id === scenario;
            return (
              <li key={id}>
                <button
                  type="button"
                  role="option"
                  aria-selected={activeItem}
                  onClick={() => {
                    setScenario(id);
                    setOpen(false);
                  }}
                  className={cn(
                    "flex w-full items-center justify-between border-l-2 px-margin-md py-margin-sm text-left transition-colors",
                    activeItem
                      ? "border-primary bg-surface-container-high"
                      : "border-transparent hover:bg-surface-container",
                  )}
                >
                  <span className="flex flex-col">
                    <span className="font-headline-sm text-headline-sm uppercase text-on-surface">
                      {s.label}
                    </span>
                    <span className="font-data-mono text-label-xs text-on-surface-variant">
                      {s.demoScenario} · {s.controlEnabled ? "CLOSED" : "OPEN"} · {s.durationS}s
                    </span>
                  </span>
                  {activeItem && <Check className="h-4 w-4 text-primary" strokeWidth={1.5} />}
                </button>
              </li>
            );
          })}
          <li className="border-t border-outline-variant px-margin-md py-unit">
            <span className="font-data-mono text-label-xs text-on-surface-variant">
              {meta ? `src: ${meta.source} · ${meta.frames} frames` : "loading telemetry…"}
            </span>
          </li>
        </ul>
      )}
    </div>
  );
}
