import React from "react";
import { cn } from "@/lib/cn";

type Variant = "ghost" | "outline" | "primary" | "toggle";

/** Rectangular, 1px border, no fill unless primary / toggled-on (design.md). */
export const Button = React.forwardRef<
  HTMLButtonElement,
  React.ButtonHTMLAttributes<HTMLButtonElement> & {
    variant?: Variant;
    active?: boolean;
    square?: boolean;
  }
>(function Button(
  { variant = "outline", active = false, square = false, className, children, ...rest },
  ref,
) {
  const base =
    "inline-flex items-center justify-center gap-margin-sm font-headline-sm text-headline-sm uppercase tracking-wider transition-colors disabled:opacity-40 disabled:cursor-not-allowed";
  const pad = square ? "h-8 w-8" : "px-margin-md py-margin-sm";
  const styles: Record<Variant, string> = {
    ghost: "border border-outline-variant text-on-surface hover:border-primary hover:text-primary bg-surface",
    outline:
      "border border-outline-variant text-on-surface hover:bg-surface-container hover:border-primary/60",
    primary: "border border-primary bg-primary text-on-primary hover:bg-primary-fixed",
    toggle: active
      ? "border border-primary bg-surface-variant text-primary"
      : "border border-outline-variant text-on-surface hover:border-primary hover:text-primary bg-surface",
  };
  return (
    <button ref={ref} className={cn(base, pad, styles[variant], className)} {...rest}>
      {children}
    </button>
  );
});
