"use client";

import React, { useEffect, useRef, useState } from "react";
import { cn } from "@/lib/cn";

/**
 * Telemetry value wrapper. When the rendered string changes it briefly washes
 * the background with `--flash-bg` (mint @ 16%) so a scanning operator can see
 * *which* readout just updated without the digits jumping. Purely presentational
 * — it renders whatever `children` it is given and never derives state.
 *
 * Respects `prefers-reduced-motion` via the global rule in globals.css.
 */
export function AnimatedValue({
  children,
  className,
  as: As = "span",
}: {
  children: React.ReactNode;
  className?: string;
  as?: React.ElementType;
}) {
  const key = typeof children === "string" || typeof children === "number" ? String(children) : null;
  const prev = useRef(key);
  const [pulse, setPulse] = useState(0);

  useEffect(() => {
    if (key !== null && prev.current !== null && key !== prev.current) {
      setPulse((n) => n + 1);
    }
    prev.current = key;
  }, [key]);

  return (
    <As
      key={pulse}
      className={cn(
        "tnum -mx-0.5 rounded-[1px] px-0.5",
        pulse > 0 && "animate-flash",
        className,
      )}
    >
      {children}
    </As>
  );
}
