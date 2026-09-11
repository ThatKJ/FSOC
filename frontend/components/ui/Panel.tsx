import React from "react";
import { cn } from "@/lib/cn";

/** Instrument panel — sharp corners, 1px structural border, tonal surface (design.md). */
export function Panel({
  className,
  children,
  as: As = "div",
  ...rest
}: React.HTMLAttributes<HTMLElement> & { as?: React.ElementType }) {
  return (
    <As
      className={cn("bg-surface-container border border-outline-variant", className)}
      {...rest}
    >
      {children}
    </As>
  );
}

export function PanelHeader({
  title,
  right,
  accent,
  className,
}: {
  title: React.ReactNode;
  right?: React.ReactNode;
  accent?: "primary" | "warning" | "lost" | "muted";
  className?: string;
}) {
  const color =
    accent === "warning"
      ? "text-tertiary-container"
      : accent === "lost"
        ? "text-error"
        : accent === "primary"
          ? "text-primary"
          : "text-on-surface-variant";
  return (
    <div
      className={cn(
        "flex items-center justify-between border-b border-outline-variant bg-surface-container-low px-margin-md py-margin-sm",
        className,
      )}
    >
      <span className={cn("font-label-xs text-label-xs uppercase tracking-widest", color)}>
        {title}
      </span>
      {right}
    </div>
  );
}

/** small 8x8 status square — solid = active, 1px stroke = inactive (design.md) */
export function StatusSquare({
  active,
  color = "primary",
  pulse,
  className,
}: {
  active?: boolean;
  color?: "primary" | "warning" | "lost" | "detected" | "muted";
  pulse?: boolean;
  className?: string;
}) {
  const bg = {
    primary: "bg-primary border-primary",
    warning: "bg-tertiary border-tertiary",
    lost: "bg-error border-error",
    detected: "bg-secondary border-secondary",
    muted: "bg-on-surface-variant border-on-surface-variant",
  }[color];
  return (
    <span
      className={cn(
        "inline-block h-2 w-2 border",
        active ? bg : `bg-transparent ${bg.split(" ")[1]}`,
        pulse && active && "animate-pulse",
        className,
      )}
    />
  );
}

/** label-above / mono-value-below readout — the core telemetry primitive */
export function Readout({
  label,
  value,
  unit,
  tone = "default",
  align = "left",
  className,
}: {
  label: React.ReactNode;
  value: React.ReactNode;
  unit?: React.ReactNode;
  tone?: "default" | "primary" | "warning" | "lost" | "detected" | "muted";
  align?: "left" | "right";
  className?: string;
}) {
  const valueColor = {
    default: "text-on-surface",
    primary: "text-primary",
    warning: "text-tertiary-fixed",
    lost: "text-error",
    detected: "text-secondary-fixed-dim",
    muted: "text-on-surface-variant",
  }[tone];
  return (
    <div className={cn("flex flex-col gap-unit", align === "right" && "items-end", className)}>
      <span className="font-label-xs text-label-xs uppercase tracking-widest text-on-surface-variant">
        {label}
      </span>
      <span className={cn("font-data-mono text-data-mono tnum", valueColor)}>
        {value}
        {unit != null && <span className="ml-1 text-on-surface-variant">{unit}</span>}
      </span>
    </div>
  );
}

/** key/value row with a hairline underline — used in side telemetry stacks */
export function KeyValueRow({
  k,
  v,
  tone = "default",
  border = true,
}: {
  k: React.ReactNode;
  v: React.ReactNode;
  tone?: "default" | "primary" | "warning" | "lost" | "muted";
  border?: boolean;
}) {
  const valueColor = {
    default: "text-on-surface",
    primary: "text-primary-fixed",
    warning: "text-tertiary-fixed",
    lost: "text-error",
    muted: "text-on-surface-variant",
  }[tone];
  return (
    <div
      className={cn(
        "flex items-baseline justify-between pb-unit",
        border && "border-b border-outline-variant",
      )}
    >
      <span className="font-label-xs text-label-xs uppercase tracking-wide text-on-surface-variant">
        {k}
      </span>
      <span className={cn("font-data-mono text-data-mono tnum", valueColor)}>{v}</span>
    </div>
  );
}
