"use client";

import { useEffect, useRef, useState } from "react";
import { AnimatePresence, motion } from "framer-motion";
import { ChevronDown, Check } from "lucide-react";

import { SCENARIOS, SCENARIO_IDS, type ScenarioId } from "@/lib/baseline/constants";
import { useSimulation } from "@/lib/simulation/SimulationProvider";
import { cn } from "@/lib/cn";

/**
 * Apple-style scenario selector — clean dropdown with subtle animations.
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
        className="flex items-center gap-2 rounded-full border border-gray-300 bg-white px-4 py-1.5 shadow-sm transition-all duration-200 hover:border-gray-400 hover:shadow"
      >
        <span className={cn(
          "status-dot",
          status === "loading" ? "active primary" : status === "error" ? "error" : "primary"
        )} />
        <span className="text-sm font-medium text-apple-ink">{label}</span>
        <ChevronDown
          className={cn(
            "h-4 w-4 text-gray-500 transition-transform duration-200",
            open && "rotate-180"
          )}
          strokeWidth={2}
        />
      </button>

      <AnimatePresence>
        {open && (
          <motion.ul
            role="listbox"
            initial={{ opacity: 0, y: -4, scale: 0.98 }}
            animate={{ opacity: 1, y: 0, scale: 1 }}
            exit={{ opacity: 0, y: -4, scale: 0.98 }}
            transition={{ duration: 0.15 }}
            className="absolute left-1/2 top-[calc(100%+8px)] z-50 w-[280px] -translate-x-1/2 overflow-hidden rounded-xl border border-gray-200 bg-white shadow-lg"
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
                      "flex w-full items-center justify-between px-4 py-3 text-left transition-colors",
                      activeItem
                        ? "bg-apple-blue/5"
                        : "hover:bg-gray-50"
                    )}
                  >
                    <span className="flex flex-col gap-0.5">
                      <span className={cn(
                        "text-sm font-medium",
                        activeItem ? "text-apple-blue" : "text-apple-ink"
                      )}>
                        {s.label}
                      </span>
                      <span className="text-xs text-gray-500">
                        {s.demoScenario} · {s.controlEnabled ? "Closed Loop" : "Open Loop"} · {s.durationS}s
                      </span>
                    </span>
                    {activeItem && <Check className="h-4 w-4 text-apple-blue" strokeWidth={2} />}
                  </button>
                </li>
              );
            })}
            <li className="border-t border-gray-200 px-4 py-2">
              <span className="text-xs text-gray-500">
                {meta ? `Source: ${meta.source} · ${meta.frames} frames` : "Loading telemetry…"}
              </span>
            </li>
          </motion.ul>
        )}
      </AnimatePresence>
    </div>
  );
}
