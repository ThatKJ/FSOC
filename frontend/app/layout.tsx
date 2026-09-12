import type { Metadata, Viewport } from "next";
import { JetBrains_Mono } from "next/font/google";
import { GeistSans } from "geist/font/sans";

import "./globals.css";
import { SimulationProvider } from "@/lib/simulation/SimulationProvider";
import { AppShell } from "@/components/shell/AppShell";

const jetbrainsMono = JetBrains_Mono({
  subsets: ["latin"],
  variable: "--font-mono",
  display: "swap",
});

const SITE_URL = "https://fsoc-iota.vercel.app";
const TITLE = "FSOC — AI-Based Virtual Camera Tracking";
const DESCRIPTION =
  "Closed-loop hybrid perception, state estimation and pan/tilt control testbed for coarse alignment of mobile free-space optical communication terminals.";

export const metadata: Metadata = {
  metadataBase: new URL(SITE_URL),
  title: {
    default: TITLE,
    template: "%s — FSOC",
  },
  description: DESCRIPTION,
  applicationName: "FSOC Mission Control",
  keywords: [
    "FSOC",
    "free-space optical communication",
    "computer vision",
    "tracking",
    "control systems",
    "SIH26169",
  ],
  authors: [{ name: "Team IRODOV" }],
  robots: { index: true, follow: true },
  openGraph: {
    type: "website",
    url: SITE_URL,
    siteName: "FSOC",
    title: TITLE,
    description: DESCRIPTION,
  },
  twitter: {
    card: "summary_large_image",
    title: TITLE,
    description: DESCRIPTION,
  },
};

export const viewport: Viewport = {
  themeColor: "#0e1514",
  colorScheme: "dark",
};

export default function RootLayout({ children }: { children: React.ReactNode }) {
  return (
    <html lang="en" className={`${GeistSans.variable} ${jetbrainsMono.variable}`}>
      <body className="select-none bg-background font-body-md text-on-surface">
        <SimulationProvider>
          <AppShell>{children}</AppShell>
        </SimulationProvider>
      </body>
    </html>
  );
}
