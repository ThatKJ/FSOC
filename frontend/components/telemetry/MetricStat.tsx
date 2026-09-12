import React from "react";
import { cn } from "@/lib/cn";

type Tone = "default" | "primary" | "warning" | "lost";

/** Big display-telem readout with a label + optional status word (Stitch metrics bar). */
export function MetricStat({
  label,
  value,
  unit,
  tone = "default",
  status,
  pulse,
  className,
}: {
  label: React.ReactNode;
  value: React.ReactNode;
  unit?: React.ReactNode;
  tone?: Tone;
  status?: React.ReactNode;
  pulse?: boolean;
  className?: string;
}) {
  const color = {
    default: "text-on-surface",
    primary: "text-primary",
    warning: "text-tertiary",
    lost: "text-error",
  }[tone];
  const statusColor = {
    default: "text-on-surface-variant",
    primary: "text-primary",
    warning: "text-tertiary",
    lost: "text-error",
  }[tone];
  return (
    <div className={cn("flex flex-col justify-center px-margin-md", className)}>
      <div className="mb-1 flex items-center gap-2 whitespace-nowrap font-label-xs text-label-xs uppercase tracking-widest text-on-surface-variant">
        <span>{label}</span>
        {pulse && <span className="h-1.5 w-1.5 shrink-0 animate-pulse bg-primary" />}
        {status != null && (
          <span className={cn("shrink-0 border border-current px-1 py-px leading-none", statusColor)}>{status}</span>
        )}
      </div>
      <div className={cn("whitespace-nowrap font-data-mono text-display-telem tnum", color)}>
        {value}
        {unit != null && <span className="ml-0.5 align-baseline text-data-mono">{unit}</span>}
      </div>
    </div>
  );
}
