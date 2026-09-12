import type { MetadataRoute } from "next";

const SITE_URL = "https://fsoc-iota.vercel.app";

const ROUTES = [
  "",
  "/mission",
  "/tracking",
  "/world",
  "/telemetry",
  "/scenarios",
  "/benchmarks",
  "/validation",
  "/architecture",
];

export default function sitemap(): MetadataRoute.Sitemap {
  return ROUTES.map((route) => ({
    url: `${SITE_URL}${route}`,
    lastModified: new Date(),
  }));
}
