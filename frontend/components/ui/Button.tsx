import React from "react";
import { cn } from "@/lib/cn";

type Variant = "primary" | "secondary" | "outline" | "ghost";

/**
 * Apple-style button — pill radius, blue accent,
 * clean hover states, subtle shadows.
 */
export const Button = React.forwardRef<
  HTMLButtonElement,
  React.ButtonHTMLAttributes<HTMLButtonElement> & {
    variant?: Variant;
    active?: boolean;
    square?: boolean;
  }
>(function Button(
  { variant = "primary", active = false, square = false, className, children, ...rest },
  ref,
) {
  const base =
    "inline-flex items-center justify-center gap-2 font-body text-sm font-normal transition-all duration-apple focus-visible:shadow-focus disabled:opacity-40 disabled:cursor-not-allowed";

  const pad = square ? "h-10 w-10" : "px-6 py-3";

  const styles: Record<Variant, string> = {
    primary:
      "rounded-pill bg-apple-blue text-white hover:bg-apple-blue-hover active:bg-apple-blue-active hover:scale-[1.02] active:scale-[0.98]",
    secondary:
      "rounded-pill bg-apple-ink text-white hover:opacity-85 hover:scale-[1.02] active:scale-[0.98]",
    outline:
      "rounded-pill border border-apple-blue text-apple-blue bg-transparent hover:bg-apple-blue hover:text-white",
    ghost:
      "rounded-md text-apple-ink hover:bg-gray-100 active:bg-gray-200",
  };

  return (
    <button ref={ref} className={cn(base, pad, styles[variant], className)} {...rest}>
      {children}
    </button>
  );
});
