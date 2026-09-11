import React from "react";
import { TopBar } from "./TopBar";
import { NavRail } from "./NavRail";

/**
 * Apple-style shell — clean, light, premium.
 */
export function AppShell({ children }: { children: React.ReactNode }) {
  return (
    <>
      <TopBar />
      <NavRail />
      <main className="relative min-h-screen animate-fadeIn bg-apple-white pl-16 pt-12">
        {children}
      </main>
    </>
  );
}

/** Full-viewport screen frame */
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
      className={cn(
        "flex h-[calc(100vh-48px)] w-full flex-col overflow-hidden",
        pad && "gap-4 p-4",
        className
      )}
    >
      {children}
    </div>
  );
}

function cn(...classes: (string | boolean | undefined)[]) {
  return classes.filter(Boolean).join(" ");
}
