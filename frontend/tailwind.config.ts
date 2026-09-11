import type { Config } from "tailwindcss";

/**
 * Apple-Inspired Design System
 * Premium white space, SF Pro typography, Apple blue accent,
 * cinematic imagery, restrained chrome.
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
        // === APPLE CORE ===
        "apple-white": "#ffffff",
        "apple-gray": "#f5f5f7",
        "apple-gray-warm": "#fbfbfd",
        "apple-black": "#000000",
        "apple-ink": "#1d1d1f",
        "apple-blue": "#0071e3",
        "apple-blue-hover": "#0077ed",
        "apple-blue-active": "#0066cc",
        "apple-blue-light": "#2997ff",

        // Neutrals
        "gray-100": "#f5f5f7",
        "gray-200": "#e8e8ed",
        "gray-300": "#d2d2d7",
        "gray-400": "#86868b",
        "gray-500": "#6e6e73",
        "gray-600": "#424245",
        "gray-700": "#1d1d1f",

        // Status
        "status-success": "#34c759",
        "status-warning": "#ff9500",
        "status-error": "#ff3b30",

        // Legacy mappings
        background: "#ffffff",
        surface: "#f5f5f7",
        "surface-container": "#f5f5f7",
        "surface-container-low": "#fbfbfd",
        "surface-container-lowest": "#ffffff",
        "surface-container-high": "#e8e8ed",
        "surface-container-highest": "#d2d2d7",
        "surface-bright": "#ffffff",
        "surface-variant": "#f5f5f7",
        "sensor-black": "#000000",
        "on-surface": "#1d1d1f",
        "on-surface-variant": "#6e6e73",
        "on-background": "#1d1d1f",
        "outline-variant": "#d2d2d7",
        outline: "#86868b",
        primary: "#0071e3",
        "on-primary": "#ffffff",
        "primary-fixed": "#0077ed",
        "primary-container": "#e3f2fd",
        secondary: "#6e6e73",
        tertiary: "#ff9500",
        "tertiary-container": "#fff3e0",
        "tertiary-fixed": "#ff9500",
        error: "#ff3b30",
        "error-container": "#ffebee",
        tracking: "#0071e3",
        detected: "#0066cc",
        warning: "#ff9500",
        lost: "#ff3b30",
      },
      fontFamily: {
        display: ["SF Pro Display", "SF Pro Icons", "Helvetica Neue", "Helvetica", "Arial", "sans-serif"],
        body: ["SF Pro Text", "SF Pro Icons", "Helvetica Neue", "Helvetica", "Arial", "sans-serif"],
        mono: ["SF Mono", "ui-monospace", "JetBrains Mono", "Menlo", "Monaco", "Consolas", "monospace"],
        // Legacy
        "data-mono": ["SF Mono", "ui-monospace", "Menlo", "monospace"],
        "body-md": ["SF Pro Text", "Helvetica Neue", "Arial", "sans-serif"],
        "headline-sm": ["SF Pro Display", "Helvetica Neue", "Arial", "sans-serif"],
        "label-xs": ["SF Pro Text", "Helvetica Neue", "Arial", "sans-serif"],
        "display-telem": ["SF Pro Display", "Helvetica Neue", "Arial", "sans-serif"],
      },
      fontSize: {
        // Apple scale
        "xs": ["12px", { lineHeight: "1.33", letterSpacing: "-0.01em", fontWeight: "400" }],
        "sm": ["14px", { lineHeight: "1.43", letterSpacing: "-0.01em", fontWeight: "400" }],
        "base": ["17px", { lineHeight: "1.47", letterSpacing: "-0.02em", fontWeight: "400" }],
        "lg": ["21px", { lineHeight: "1.38", letterSpacing: "-0.01em", fontWeight: "400" }],
        "xl": ["28px", { lineHeight: "1.14", letterSpacing: "0.007em", fontWeight: "600" }],
        "2xl": ["40px", { lineHeight: "1.10", letterSpacing: "-0.003em", fontWeight: "600" }],
        "3xl": ["48px", { lineHeight: "1.08", letterSpacing: "-0.003em", fontWeight: "600" }],
        "4xl": ["56px", { lineHeight: "1.07", letterSpacing: "-0.005em", fontWeight: "600" }],
        "5xl": ["80px", { lineHeight: "1.05", letterSpacing: "-0.015em", fontWeight: "600" }],
        // Legacy
        "data-mono": ["17px", { lineHeight: "1.29", letterSpacing: "-0.02em", fontWeight: "500" }],
        "body-md": ["17px", { lineHeight: "1.47", letterSpacing: "-0.02em", fontWeight: "400" }],
        "headline-sm": ["14px", { lineHeight: "1.29", letterSpacing: "-0.01em", fontWeight: "600" }],
        "label-xs": ["12px", { lineHeight: "1.33", letterSpacing: "-0.01em", fontWeight: "400" }],
        "display-telem": ["28px", { lineHeight: "1.14", letterSpacing: "-0.01em", fontWeight: "600" }],
      },
      spacing: {
        "unit": "4px",
        "gutter": "1px",
        "margin-sm": "8px",
        "margin-md": "16px",
        "margin-lg": "24px",
        "panel-padding": "16px",
      },
      borderRadius: {
        none: "0",
        sm: "8px",
        DEFAULT: "12px",
        md: "12px",
        lg: "18px",
        xl: "24px",
        "2xl": "28px",
        full: "9999px",
        pill: "980px",
      },
      boxShadow: {
        none: "none",
        sm: "0 1px 3px rgba(0, 0, 0, 0.08)",
        DEFAULT: "0 4px 12px rgba(0, 0, 0, 0.08)",
        md: "0 4px 12px rgba(0, 0, 0, 0.08)",
        lg: "0 12px 32px rgba(0, 0, 0, 0.12)",
        focus: "0 0 0 4px rgba(0, 113, 227, 0.35)",
        edge: "0 0 0 1px rgba(0, 0, 0, 0.04)",
        pop: "0 12px 32px rgba(0, 0, 0, 0.15)",
      },
      transitionTimingFunction: {
        apple: "cubic-bezier(0.28, 0, 0.22, 1)",
        "out-expo": "cubic-bezier(0.16, 1, 0.3, 1)",
      },
      transitionDuration: {
        apple: "220ms",
        fast: "150ms",
      },
      keyframes: {
        fadeIn: {
          "0%": { opacity: "0" },
          "100%": { opacity: "1" },
        },
        slideUp: {
          "0%": { opacity: "0", transform: "translateY(8px)" },
          "100%": { opacity: "1", transform: "translateY(0)" },
        },
        pulse: {
          "0%, 100%": { opacity: "1", transform: "scale(1)" },
          "50%": { opacity: "0.6", transform: "scale(0.95)" },
        },
      },
      animation: {
        fadeIn: "fadeIn 400ms cubic-bezier(0.28, 0, 0.22, 1) both",
        slideUp: "slideUp 500ms cubic-bezier(0.28, 0, 0.22, 1) both",
        pulse: "pulse 2s ease-in-out infinite",
        fade: "fadeIn 220ms cubic-bezier(0.28, 0, 0.22, 1) both",
        rise: "slideUp 300ms cubic-bezier(0.28, 0, 0.22, 1) both",
      },
    },
  },
  plugins: [],
};

export default config;
