import React from "react";
import { TopBar } from "./TopBar";
import { NavRail } from "./NavRail";

/** Header (48px) + left rail (64px) + main. Matches the Stitch shell exactly. */
export function AppShell({ children }: { children: React.ReactNode }) {
  return (
    <>
      <TopBar />
      <NavRail />
      <main className="relative min-h-screen bg-background pl-[64px] pt-[48px]">{children}</main>
    </>
  );
}

/** Standard page frame: fills the viewport below the header. */
export function Screen({
  children,
  className = "",
  pad = false,
}: {
  children: React.ReactNode;
  className?: string;
  pad?: boolean;
}) {
  return (
    <div
      className={`flex h-[calc(100vh-48px)] w-full flex-col overflow-hidden ${
        pad ? "p-margin-md gap-margin-md" : ""
      } ${className}`}
    >
      {children}
    </div>
  );
}
