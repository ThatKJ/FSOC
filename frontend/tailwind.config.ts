import type { Config } from "tailwindcss";

/**
 * Design tokens are the "Orbital Precision System" design system defined inside
 * the FSOC Stitch project (design.md + the per-screen tailwind.config blocks).
 * Values below are copied verbatim from Stitch — do not substitute an aesthetic.
 * See frontend/DESIGN_SYSTEM.md for the extraction notes.
 */
const config: Config = {
  darkMode: "class",
  content: [
    "./app/**/*.{ts,tsx}",
    "./components/**/*.{ts,tsx}",
    "./lib/**/*.{ts,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        // --- Stitch palette (exact) ---
        "tertiary-fixed": "#ffddba",
        primary: "#6feee1",
        "on-secondary": "#00344f",
        "secondary-fixed": "#cbe6ff",
        "tertiary-fixed-dim": "#ffb866",
        "on-secondary-fixed": "#001e30",
        "on-tertiary-fixed-variant": "#673d00",
        "primary-fixed": "#79f7ea",
        "error-container": "#93000a",
        outline: "#869491",
        "primary-fixed-dim": "#5adace",
        "tertiary-container": "#f8af57",
        "on-primary-fixed": "#00201d",
        "surface-variant": "#303635",
        "on-primary-container": "#005750",
        "on-primary-fixed-variant": "#00504a",
        "surface-container-highest": "#303635",
        "surface-container-low": "#171d1c",
        "inverse-on-surface": "#2b3231",
        "surface-container": "#1b2120",
        "surface-container-lowest": "#090f0e",
        background: "#0e1514",
        "surface-dim": "#0e1514",
        "on-primary": "#003733",
        "on-tertiary-container": "#6f4300",
        "on-surface": "#dee4e2",
        "on-background": "#dee4e2",
        "surface-bright": "#343a39",
        "on-surface-variant": "#bbc9c7",
        "inverse-surface": "#dee4e2",
        "on-tertiary-fixed": "#2b1700",
        "secondary-fixed-dim": "#8ecdff",
        "on-secondary-container": "#deeeff",
        "primary-container": "#4fd1c5",
        error: "#ffb4ab",
        "surface-tint": "#5adace",
        "on-tertiary": "#482900",
        "inverse-primary": "#006a63",
        surface: "#0e1514",
        "outline-variant": "#3c4947",
        "secondary-container": "#0071a7",
        "on-error-container": "#ffdad6",
        tertiary: "#ffd2a2",
        "on-secondary-fixed-variant": "#004b71",
        secondary: "#8ecdff",
        "surface-container-high": "#252b2a",
        "on-error": "#690005",
        // deepest "space" black used by the optical viewports (design.md L0)
        "sensor-black": "#060807",
        // --- semantic aliases (map straight to the palette; single source) ---
        tracking: "#6feee1", // TRACKING / nominal lock       (== primary)
        detected: "#8ecdff", // detection / acquisition vector (== secondary)
        warning: "#ffd2a2", // rate saturation / caution       (== tertiary)
        lost: "#ffb4ab", // TARGET_LOST / failure               (== error)
      },
      borderRadius: {
        none: "0",
        sm: "0.125rem",
        DEFAULT: "0.25rem",
        lg: "0.5rem",
        xl: "0.75rem",
        full: "9999px",
      },
      spacing: {
        gutter: "1px",
        "panel-padding": "12px",
        "margin-sm": "8px",
        unit: "4px",
        "margin-md": "16px",
        "margin-lg": "24px",
      },
      fontFamily: {
        "data-mono": ["var(--font-mono)", "ui-monospace", "monospace"],
        "body-md": ["var(--font-geist-sans)", "ui-sans-serif", "system-ui", "sans-serif"],
        "headline-sm": ["var(--font-geist-sans)", "ui-sans-serif", "system-ui", "sans-serif"],
        "label-xs": ["var(--font-mono)", "ui-monospace", "monospace"],
        "display-telem": ["var(--font-mono)", "ui-monospace", "monospace"],
      },
      fontSize: {
        "data-mono": ["12px", { lineHeight: "16px", fontWeight: "400" }],
        "body-md": ["13px", { lineHeight: "18px", fontWeight: "400" }],
        "headline-sm": [
          "14px",
          { lineHeight: "20px", letterSpacing: "0.05em", fontWeight: "600" },
        ],
        "label-xs": ["10px", { lineHeight: "12px", fontWeight: "500" }],
        "display-telem": [
          "24px",
          { lineHeight: "32px", letterSpacing: "-0.02em", fontWeight: "500" },
        ],
      },
      keyframes: {
        scan: {
          "0%": { left: "0%", opacity: "0" },
          "5%": { opacity: "1" },
          "95%": { opacity: "1" },
          "100%": { left: "100%", opacity: "0" },
        },
        dash: { to: { strokeDashoffset: "-24" } },
        slideRight: {
          "0%": { transform: "translateX(0)", opacity: "0" },
          "10%": { opacity: "1" },
          "90%": { opacity: "1" },
          "100%": { transform: "translateX(100%)", opacity: "0" },
        },
        sweep: {
          "0%": { transform: "translateX(-100%)" },
          "100%": { transform: "translateX(300%)" },
        },
      },
      animation: {
        scan: "scan 5s linear infinite",
        dash: "dash 1s linear infinite",
        "slide-right": "slideRight 2s linear infinite",
        sweep: "sweep 1.5s ease-in-out infinite",
        "spin-slow": "spin 60s linear infinite",
      },
    },
  },
  plugins: [],
};

export default config;
