import React from "react";
import { cn } from "@/lib/cn";

/**
 * Apple-style panel — white/pale gray surface with soft shadow,
 * rounded corners, clean borders.
 */
export function Panel({
  className,
  children,
  as: As = "div",
  dark,
  ...rest
}: React.HTMLAttributes<HTMLElement> & { as?: React.ElementType; dark?: boolean }) {
  return (
    <As
      className={cn(
        "rounded-lg overflow-hidden transition-all duration-apple",
        dark ? "bg-sensor-black border border-gray-600" : "bg-white shadow-sm border border-gray-200",
        className,
      )}
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
      ? "text-status-warning"
      : accent === "lost"
        ? "text-status-error"
        : accent === "primary"
          ? "text-apple-blue"
          : "text-gray-500";

  return (
    <div
      className={cn(
        "flex items-center justify-between border-b border-gray-200 px-4 py-3 bg-gray-100/50",
        className,
      )}
    >
      <span className={cn("text-xs font-semibold tracking-wide uppercase", color)}>
        {title}
      </span>
      {right}
    </div>
  );
}

/** Status indicator — 8px circle, pulsing when active */
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
  const colors = {
    primary: active ? "bg-apple-blue" : "bg-gray-300",
    warning: active ? "bg-status-warning" : "bg-gray-300",
    lost: active ? "bg-status-error" : "bg-gray-300",
    detected: active ? "bg-apple-blue-active" : "bg-gray-300",
    muted: active ? "bg-gray-500" : "bg-gray-300",
  }[color];

  return (
    <span
      className={cn(
        "inline-block w-2 h-2 rounded-full transition-all",
        colors,
        pulse && active && "animate-pulse",
        className,
      )}
    />
  );
}

/** Data readout — label above, mono value below */
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
    default: "text-apple-ink",
    primary: "text-apple-blue",
    warning: "text-status-warning",
    lost: "text-status-error",
    detected: "text-apple-blue-active",
    muted: "text-gray-500",
  }[tone];

  return (
    <div className={cn("flex flex-col gap-1", align === "right" && "items-end", className)}>
      <span className="text-xs font-normal text-gray-500">
        {label}
      </span>
      <span className={cn("font-mono text-base font-medium tnum", valueColor)}>
        {value}
        {unit != null && <span className="ml-1 text-gray-400">{unit}</span>}
      </span>
    </div>
  );
}

/** Key/value row with optional border */
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
    default: "text-apple-ink",
    primary: "text-apple-blue",
    warning: "text-status-warning",
    lost: "text-status-error",
    muted: "text-gray-500",
  }[tone];

  return (
    <div
      className={cn(
        "flex items-baseline justify-between py-2 px-4",
        border && "border-b border-gray-200",
      )}
    >
      <span className="text-sm font-normal text-gray-600">{k}</span>
      <span className={cn("font-mono text-sm font-medium tnum", valueColor)}>{v}</span>
    </div>
  );
}
