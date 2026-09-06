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

export const metadata: Metadata = {
  title: "FSOC ALIGNMENT — SIH26169",
  description:
    "Mission-control frontend for the SIH26169 virtual camera tracking engine. Observer / presentation layer over the frozen v1_baseline C++ engine.",
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
