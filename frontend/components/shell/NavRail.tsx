"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { motion } from "framer-motion";

import { NAV_ITEMS } from "@/lib/nav";
import { cn } from "@/lib/cn";

/**
 * Apple-style nav rail — clean, minimal, with subtle active states.
 */
export function NavRail() {
  const pathname = usePathname();

  return (
    <aside className="fixed bottom-0 left-0 top-12 z-40 flex w-16 flex-col items-center border-r border-gray-300 bg-apple-gray py-4">
      <nav className="flex w-full flex-col gap-1">
        {NAV_ITEMS.map((item) => {
          const active =
            item.path === "/"
              ? pathname === "/"
              : pathname === item.path || pathname.startsWith(`${item.path}/`);
          const Icon = item.icon;

          return (
            <Link
              key={item.path}
              href={item.path}
              title={item.label}
              aria-label={item.label}
              aria-current={active ? "page" : undefined}
              className={cn(
                "group relative flex h-11 w-full items-center justify-center transition-colors duration-200",
                active
                  ? "text-apple-blue"
                  : "text-gray-500 hover:bg-gray-200/50 hover:text-apple-ink"
              )}
            >
              {/* Active indicator */}
              {active && (
                <motion.span
                  layoutId="nav-active"
                  transition={{ type: "spring", stiffness: 500, damping: 35 }}
                  className="absolute left-0 h-6 w-1 rounded-r-full bg-apple-blue"
                />
              )}

              <Icon className="h-5 w-5" strokeWidth={1.75} />

              {/* Tooltip */}
              <span className="pointer-events-none absolute left-full ml-3 whitespace-nowrap rounded-lg bg-apple-ink px-3 py-1.5 text-xs font-medium text-white opacity-0 shadow-lg transition-opacity group-hover:opacity-100">
                {item.label}
              </span>
            </Link>
          );
        })}
      </nav>
    </aside>
  );
}
