/**
 * Shared framer-motion variants for the "Mk II" elevation pass.
 *
 * Motion here is functional, not decorative: it communicates mount/unmount,
 * acquisition, and value change. Every transition is short (<= 340ms) and the
 * global `prefers-reduced-motion` rule in globals.css collapses them to ~0ms.
 */
import type { Transition, Variants } from "framer-motion";

/** primary easing — matches --ease-out in globals.css */
export const EASE_OUT = [0.16, 1, 0.3, 1] as [number, number, number, number];
export const EASE_IN_OUT = [0.76, 0, 0.24, 1] as [number, number, number, number];

export const DUR = { fast: 0.12, base: 0.2, slow: 0.34 } as const;

/** panel / section entrance — a small rise + fade */
export const rise: Variants = {
  hidden: { opacity: 0, y: 6 },
  visible: { opacity: 1, y: 0, transition: { duration: DUR.slow, ease: EASE_OUT } },
  exit: { opacity: 0, y: 4, transition: { duration: DUR.fast, ease: EASE_OUT } },
};

/** overlay / popover entrance — scale from the anchored edge */
export const popover: Variants = {
  hidden: { opacity: 0, y: -4, scale: 0.98 },
  visible: { opacity: 1, y: 0, scale: 1, transition: { duration: DUR.base, ease: EASE_OUT } },
  exit: { opacity: 0, y: -4, scale: 0.98, transition: { duration: DUR.fast, ease: EASE_OUT } },
};

/** stagger container for lists of readouts / rows */
export const stagger: Variants = {
  hidden: {},
  visible: { transition: { staggerChildren: 0.03 } },
};

/** shared layout transition for the nav-rail active marker */
export const markerTransition: Transition = {
  type: "spring",
  stiffness: 520,
  damping: 40,
  mass: 0.6,
};
