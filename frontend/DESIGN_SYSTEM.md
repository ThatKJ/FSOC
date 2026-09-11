# FSOC Design System — "Orbital Precision System"

**Extracted from the FSOC Stitch project** (`projects/4731868309872967927`) — the
`designTheme.designMd` block plus the per-screen `tailwind.config` in every
generated screen. Nothing here is invented. Implemented in
`tailwind.config.ts` (palette / spacing / type) and `app/globals.css` (semantic
CSS variables).

Aesthetic: **High-Density Minimalism / Technical Brutalism** — an
ultra-utilitarian mission-control interface (ISRO / SpaceX ground-station feel).
No gradients as decoration, no rounded buttons, no drop shadows for elevation.
1px hairlines replace margins. Every pixel is functional.

---

## Colors

Full Material-style token set (Stitch names → hex). The Tailwind palette carries
every one of these under its Stitch name (`bg-surface-container-low`,
`text-primary`, `border-outline-variant`, …).

### Foundation / surfaces (tonal z-index — depth via tone, not shadow)

| token | hex | role |
|---|---|---|
| `background` / `surface` / `surface-dim` | `#0e1514` | app ground |
| `surface-container-lowest` | `#090f0e` | header, deepest panels |
| `surface-container-low` | `#171d1c` | nav rail, side panels |
| `surface-container` | `#1b2120` | instrument panels |
| `surface-container-high` | `#252b2a` | raised / selected rows |
| `surface-container-highest` / `surface-variant` | `#303635` | most-interactive, toggled-on |
| `surface-bright` | `#343a39` | hover surface |
| `sensor-black` | `#060807` | optical viewports (design.md "Level 0") |

### Text

| token | hex | role |
|---|---|---|
| `on-surface` | `#dee4e2` | primary text / data values |
| `on-surface-variant` | `#bbc9c7` | labels, secondary text |

### Structural lines

| token | hex | role |
|---|---|---|
| `outline-variant` | `#3c4947` | **the** hairline border (replaces margins) |
| `outline` | `#869491` | stronger separators, calibration ticks |

### Status semantics (design.md)

| token | hex | meaning | semantic alias |
|---|---|---|---|
| `primary` | `#6feee1` | **TRACKING** / nominal lock / healthy | `--tracking`, `tracking` |
| `primary-container` / `surface-tint` | `#4fd1c5` / `#5adace` | strong mint | `--tracking-strong` |
| `secondary` | `#8ecdff` | **detection** / acquisition vectors / external objects | `--detected`, `detected` |
| `tertiary` | `#ffd2a2` | **rate saturation** / sensor clipping / caution | `--warning`, `warning` |
| `tertiary-container` | `#f8af57` | strong amber | |
| `error` | `#ffb4ab` | **TARGET LOST** / system failure / override | `--lost`, `lost` |
| `error-container` | `#93000a` | critical fill | `--lost-container` |
| `on-primary` | `#003733` | text on a mint fill | `--on-tracking` |

The design.md also names the reference hexes (Status Mint `#4FD1C5`, Detection
Cyan `#63B3ED`, Saturation Amber `#F6AD55`, Critical Red `#F56565`) — the
per-screen palette above is the one actually used and is what we implement.

---

## Typography

Dual-font strategy (design.md): **Geist Sans** for UI chrome / headings /
labels; **JetBrains Mono** for all telemetry, coordinates and tabular data (so
digits don't jitter as they update).

- `--font-geist-sans` — `geist/font/sans` (`GeistSans`)
- `--font-mono` — `next/font/google` `JetBrains_Mono`

| Tailwind name | size / line-height | weight | tracking | font | use |
|---|---|---|---|---|---|
| `display-telem` | 24 / 32 | 500 | −0.02em | mono | hero numbers, page titles |
| `headline-sm` | 14 / 20 | 600 | +0.05em | Geist | panel headers, buttons (uppercase) |
| `body-md` | 13 / 18 | 400 | — | Geist | prose |
| `data-mono` | 12 / 16 | 400 | — | mono | telemetry values |
| `label-xs` | 10 / 12 | 500 | (wide when uppercase) | mono | field labels |

Headlines are uppercase with wide tracking; labels are `uppercase tracking-widest`.

---

## Spacing (4px base — "Rigid Engineering Grid")

| token | value |
|---|---|
| `gutter` | 1px (structural separators between butted panels) |
| `unit` | 4px |
| `margin-sm` | 8px |
| `panel-padding` | 12px |
| `margin-md` | 16px |
| `margin-lg` | 24px (Architecture pipeline canvas; not in Stitch config but used in markup) |

Panels are "butted" against each other and separated by `gap-gutter` on a
`bg-outline-variant` parent so the 1px gap reads as a hairline.

---

## Shape

**Sharp-Edge Geometry.** Corners default to **0px**. `borderRadius` tokens from
Stitch are kept (`DEFAULT` 0.25rem, `lg` 0.5rem, `xl` 0.75rem, `full` 9999px)
because the markup uses `rounded-sm` (Architecture nodes), `rounded-lg`
(one panel) and `rounded-full` (avatar, lock rings, status dots) — but nothing
is rounded unless the Stitch markup rounds it. Action buttons: 1px border, no
fill unless `primary` / toggled-on.

Calibration ticks: small `1×4px` / `2×8px` rectangles at viewport corners.

---

## Elevation & motion — "Mk II" pass

An additive refinement layer. It does **not** change the palette, type scale, or
spacing, and it does not reintroduce the things design.md forbids (no decorative
gradients, no blurred drop-shadows *on the plane*, still dark-only, still sharp
corners).

### Elevation — tone + 1px inset light

Depth is still carried by surface tone. A raised panel additionally gets a
single 1px inset top highlight so it reads as lifted without a blur.

| token / class | value | use |
|---|---|---|
| `--edge-light` | `rgba(233,246,244,0.05)` | 1px inset top highlight |
| `--edge-shade` | `rgba(0,0,0,0.28)` | 1px inset bottom shade (recessed wells) |
| `shadow-edge` (Tailwind) / `.edge-lit` | `inset 0 1px 0 var(--edge-light)` | raised panels, header, nav rail, buttons |
| `.edge-well` | inset shade + 1px border | sunken readouts / optical viewports |
| `.vignette` | radial corner-darkening (tone only) | optical viewports |
| `shadow-pop` (Tailwind) | `0 12px 32px -12px rgba(0,0,0,.6)` + inset light | **overlays only** (menus / popovers that float off the plane) |

`shadow-pop` is the single sanctioned blurred shadow and is reserved for true
overlays — never for a panel sitting in the layout.

### Motion tokens

| token | value | use |
|---|---|---|
| `--dur-1` / `--dur-2` / `--dur-3` | `120 / 200 / 340 ms` | state change · fade · entrance |
| `--ease-out` | `cubic-bezier(0.16, 1, 0.3, 1)` (`ease-out-expo`) | default |
| `--ease-in-out` | `cubic-bezier(0.76, 0, 0.24, 1)` (`in-out-quart`) | reversible transitions |
| `--focus-ring` | `var(--tracking)` | 2px `:focus-visible` outline, offset 1px (global) |
| `--flash-bg` | `rgba(111,238,225,0.16)` (`--tracking` @ 16 %) | telemetry value-change wash |

Animations: `animate-rise` (entrance, 6px up + fade), `animate-fade`,
`animate-flash` (value tick). All decorative loops + entrances collapse to ~0 ms
under `prefers-reduced-motion: reduce` (global rule in `globals.css`).

Motion is functional only — mount/unmount, acquisition, and value change.
Framer-motion variants live in `components/ui/motion.ts`; `AnimatedValue`
(`components/ui/Panel.tsx` readouts) applies the flash wash.

---

## Instrument styles

- **Status square** — 8×8px (`h-2 w-2`); solid fill = active, 1px stroke =
  inactive. Often `animate-pulse` when live.
- **Telemetry readout** — `label-xs` (variant colour) stacked above a
  `data-mono` value (primary / status colour), `tabular-nums`.
- **Key/value row** — label left, mono value right, 1px bottom hairline.
- **Charts / sparklines** — thin 1px stroke, **no area fill**, dashed grid
  lines (`stroke-dasharray "2 2"`, `outline-variant`). Nominal series = mint,
  threshold-crossing / caution = amber, open-loop / failure = red.
- **Crosshair** — 1px lines with a central gap; detection reticle = mint box
  with corner notches + amber/tertiary lock ring.
- **Buttons** — rectangular, 1px border, `headline-sm` uppercase text; ghost
  (no fill) by default, `bg-primary text-on-primary` for the primary action,
  `bg-surface-variant text-primary` for toggled-on.
- **Event marker chip** — 1px border, `label-xs`, amber for warnings / red
  (pulsing) for critical.

---

## Shell dimensions (exact, from every screen)

| element | value |
|---|---|
| header height | `48px` — `bg-surface-container-lowest`, `border-b outline-variant` |
| nav rail width | `64px` — `bg-surface-container-low`, `border-r outline-variant` |
| main offset | `pt-[48px] pl-[64px]` |
| Mission Control / Validation side rail | `320px` |
| Mission Control bottom band | `120px` |
| transport bar | `72px` |
| telemetry metrics bar | `64px` |

Nav active state: `text-primary` + `border-r-2 border-primary`.

---

## Icons

Stitch uses **Material Symbols Outlined**. We use **`lucide-react`** with the
closest glyph (full mapping in `STITCH_IMPLEMENTATION_MAP.md`). Nav rail:
`grid_view→LayoutGrid`, `target→Crosshair`, `center_focus_strong→ScanEye`,
`language→Globe`, `query_stats→Activity`, `account_tree→Network`, `speed→Gauge`,
`fact_check→ClipboardCheck`, `schema→Workflow`.

---

## Theme-mode

The interface is **dark-only by design** (a "dark room" for long-duration
monitoring, per design.md). `color-scheme: dark`; no light palette.
