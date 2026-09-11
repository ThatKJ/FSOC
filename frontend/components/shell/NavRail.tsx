"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";

import { NAV_ITEMS } from "@/lib/nav";
import { cn } from "@/lib/cn";

/** Fixed left rail, w-64. Icon-only nav; active = text-primary + right accent (Stitch). */
export function NavRail() {
  const pathname = usePathname();

  return (
    <aside className="fixed bottom-0 left-0 top-[48px] z-40 flex w-[64px] flex-col items-center border-r border-outline-variant bg-surface-container-low py-margin-md">
      <nav className="flex w-full flex-col gap-gutter">
        {NAV_ITEMS.map((item) => {
          const active =
            item.path === "/" ? pathname === "/" : pathname === item.path || pathname.startsWith(`${item.path}/`);
          const Icon = item.icon;
          return (
            <Link
              key={item.path}
              href={item.path}
              title={item.label}
              aria-label={item.label}
              aria-current={active ? "page" : undefined}
              className={cn(
                "flex w-full flex-col items-center justify-center py-margin-md transition-all",
                active
                  ? "border-r-2 border-primary text-primary"
                  : "text-on-surface-variant hover:text-on-surface",
              )}
            >
              <Icon className="h-5 w-5" strokeWidth={1.75} />
            </Link>
          );
        })}
      </nav>
    </aside>
  );
}
