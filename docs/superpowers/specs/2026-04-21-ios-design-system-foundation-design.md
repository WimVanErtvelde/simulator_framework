# IOS Design System Foundation + Chrome Refactor (Spec A)

**Date**: 2026-04-21
**Scope**: `ios/frontend/` (React + Zustand IOS frontend)
**Status**: Brainstormed, awaiting implementation plan

---

## Problem

The IOS frontend at `ios/frontend/` has three related issues:

1. **Doesn't fit at 100% zoom on the target 1920×1080 touchscreen.** The instructor station currently runs at 80% browser zoom to see all controls. Cause: ~96px of vertical chrome (3-row status strip) plus 60px ActionBar plus 44px panel header = 200px before the map/panel content even starts, on top of browser chrome and OS taskbar.
2. **Status strip is three rows.** Sim state, environment readouts, and avionics are stacked. The user wants a more compact layout that uses the available width of a 1920×1080 fullscreen browser.
3. **Inconsistent visual language across panels.** 428 inline `fontSize`/`padding`/`border`/`height`/`background` literals across 36 component files, no shared token system, two parallel weather panels (`WeatherPanel` and `WeatherPanelV2`) with different style helpers. Means every new panel reinvents wheels and visual drift accumulates.

A fourth, smaller annoyance: the bottom-right `DISC (974)` indicator in `ActionBar.jsx:174` is opaque — it's actually the WebSocket reconnect counter while the backend is unreachable.

---

## Goals

- Eliminate the need for browser zoom on a 1920×1080 fullscreen IOS.
- Replace the 3-row status strip with a 2-row tier-prioritized layout.
- Establish a token-based design system (colors, spacing, typography, primitives) that future panel work can adopt.
- Replace the cryptic `DISC (974)` with a clear connection indicator.

## Non-goals (deferred to Spec B or later)

- Migrating the 11 existing panels (`AircraftPanel`, `FailuresPanel`, `InspectorPanel`, `NodesPanel`, `PositionPanel`, `ScenariosPanel`, `SessionPanel`, `TimePanel`, `WeatherPanel`, `WeatherPanelV2`) to use the new primitives. They keep working through a `PanelUtils.jsx` compat shim.
- Modal, Tabs, Table primitives.
- Light theme support (tokens are structured to allow it later; only dark ships).
- Cockpit pages (`/cockpit/c172/*`) — separate layout, addressed when Spec B reaches them.
- Removing duplicate `WeatherPanel` / `WeatherPanelV2`.
- Touchscreen-specific gesture handling beyond hit-area sizing.

---

## Architecture

A new directory `ios/frontend/src/theme/`:

```
src/theme/
├── tokens.js          single source of truth (JS object)
├── tokens.css         CSS variables on :root + global rem scale
├── primitives.jsx     shared components (Button, Badge, Readout, Stack, …)
└── index.js           re-exports
```

A `<ThemeStyles />` component is mounted once in `App.jsx`. It injects `tokens.css` and a global rule:

```css
html { font-size: clamp(15px, 0.95vw, 17px); }
```

This is the **single global UI scale knob**. All primitives use `rem` for sizes that should scale; pixel literals only for things that should not (1px borders, exact icon sizes).

`PanelUtils.jsx` becomes a thin compat shim that re-exports from `theme/primitives.jsx` so existing panels keep working unchanged. They will not look pixel-identical (rem scaling shifts sizes slightly) but they will function and remain readable. Spec B migrates them properly.

---

## Token catalogue

### Colors (semantic, not literal)

```
color.bg.app          #0d1117    main background
color.bg.panel        #0a0e17    side panel background
color.bg.subtle       #1c1c1c    inactive badge bg, dropdown rest
color.bg.hover        #21262d    dropdown hover

color.fg.primary      #e6edf3    main text
color.fg.secondary    #e2e8f0    panel value text
color.fg.muted        #64748b    labels (the dominant "gray label" tone)
color.fg.dim          #8b949e    inactive text

color.border          #1e293b    1px borders
color.border.subtle   #30363d    separators

color.accent          #39d0d8    section headers, TRUTH label
color.accent.bright   #00ff88    highlighted values, primary CTA tint
```

### State palettes (badge `bg` / `fg` / `border` triplets)

```
state.running         #1a4731 / #3fb950 / #2d6a4f    green
state.frozen          #1a2744 / #388bfd / #2d4a8f    blue
state.ready           #3d2b0a / #d29922 / #7d5a14    amber
state.init            #1c1c1c / #8b949e / #30363d    neutral
state.repositioning   #3b1f6e / #c4b5fd / #6d3fc0    violet
state.error           #3d0a0a / #f85149 / #8f2d2d    red
state.confirm         #4a1060 / #bc4fcb / #7a2090    magenta
```

### Spacing (rem-based, scale via global root font-size)

```
space.xs    0.25rem    ~4px
space.sm    0.5rem     ~8px
space.md    0.875rem   ~14px
space.lg    1rem       ~16px
space.xl    1.5rem     ~24px
```

### Typography

```
font.mono              'JetBrains Mono', 'Fira Code', 'SF Mono', monospace
font.ui                system-ui, -apple-system, sans-serif

fontSize.xxs    ~12px    section headers (was 11px — below WCAG floor)
fontSize.xs     ~14px    buttons, dropdown labels (was 13px)
fontSize.sm     ~15px    status-strip readouts (density-tuned)
fontSize.md     ~16px    panel body, default (WCAG default)
fontSize.lg     ~18px    panel emphasized values
fontSize.xl     ~22px    primary readouts (TRUTH ALT, IAS, HDG)
fontSize.xxl    ~28px    state badge, large numerics

weight.normal   400
weight.medium   500     (better than 400 for body in mono)
weight.bold     700

letter.wide     1.5px   section headers, button labels (was 2px)
```

### Radii / sizes

```
radius.sm        3px    panel buttons
radius.md        4px    action buttons, dropdowns
radius.lg        8px    state badges

size.touchMin    44px   minimum interactive height
size.touchPrimary 48px  primary action buttons
```

### Readability rationale

Defaults are sized for a **touchscreen at ~60–80cm viewing distance**:

- WCAG 2.1 recommends 16px body default.
- Apple HIG: 17pt minimum body for touch surfaces.
- Material Design 3: 14sp body / 16sp+ headline.
- Aviation cockpit displays (ARP4102): 14pt+ for primary readouts.
- NN/g control-room guidance: 14–18px at desk distance.

Status strip stays at `sm` (15px) for density; panels default to `md` (16px); emphasized panel values use `lg` (18px); the largest glanceable readouts (state badge, primary flight readouts) use `xl`/`xxl`.

---

## Primitives

In `theme/primitives.jsx`. All consume `tokens.js`.

### Layout

- `<Stack gap direction align justify>` — flex container; replaces dozens of inline flex blocks.
- `<Sep />` — vertical separator (the `|` glyph), standardised; new variant `<Sep tone="subtle" />` renders `·` for intra-cluster separation.
- `<Card padding>` — bordered surface; replaces manual border+padding+bg combos.

### Text

- `<Label>` — uppercase, `fg.muted`, `fontSize.xs`. ("QNH", "IAS", "WIND")
- `<Value emphasis="normal|bright|warn|crit">` — `fontSize.lg` weight bold; the value paired with a Label.
- `<Readout label value unit emphasis>` — Label+Value combo. The dominant pattern in StatusStrip.
- `<SectionHeader>` — replaces existing one in PanelUtils.

### Interactive

- `<Button variant="primary|secondary|warn|crit|ghost" size="sm|md|lg" disabled isPending>` — covers `ActionBtn`, `FullWidthBtn`, `DropdownItem`, panel buttons. `lg` = 48px (touch primary), `md` = 40px (secondary), `sm` = 32px (mouse-only).
- `<Badge tone="running|frozen|ready|init|warn|crit|info">` — replaces inline Badge in StatusStrip + XPDR mode badge.
- `<TapTarget>` — invisible padding wrapper that ensures ≥44px hit area around small inline text (used for radio frequencies, XPDR in the strip).
- `<Dropdown anchor>` + `<DropdownItem>` — extracted from ActionBar.

### Form

- `<Field label hint error>` — wraps an input with label/hint/error pattern.
- `<NumericInput>`, `<TextInput>` — thin token-driven wrappers over `<input>`.

### Deliberately excluded from v1

`<Modal>`, `<Tabs>`, `<Table>`, themed inputs beyond the three above. Extract when there is a second use case.

---

## Status strip 2-row redesign

### Layout

```
┌─ Row 1 (flight + environment) ────────────────────────────────────────────────┐
│ [STATE]  SIM 00:00:00  C172  TRUTH 1234ft  IAS 99kt  VS 0fpm  HDG 270°       │
│   GS 99kt  |  QNH 1013.2hPa  WIND 270°/12kt  OAT 15°C  VIS 9999m  ●CIGI HOT  │
└──────────────────────────────────────────────────────────────────────────────┘
┌─ Row 2 (systems + avionics) ──────────────────────────────────────────────────┐
│ armed:0 active:0  |  COM1 121.50  COM2 121.50  NAV1 113.20  NAV2 113.20      │
│   ADF 1234  |  XPDR 1234 [ALT]                                                │
└──────────────────────────────────────────────────────────────────────────────┘
```

Each row is 32px tall → 64px total chrome (was 96px → +32px reclaimed).

### Tier assignments

Each `<Readout>` gets `data-tier="1|2|3"`. Tiers hide via media queries — no JS resize listeners.

| Tier | Visible at | Items |
|---|---|---|
| 1 (critical) | always | STATE, SIM time, aircraft, ALT, IAS, HDG, QNH, COM1, NAV1, XPDR |
| 2 (normal)   | ≥ 1600px | VS, GS, WIND, OAT, terrain indicator, COM2, NAV2 |
| 3 (secondary)| ≥ 1920px | VIS, armed/active failures, ADF |

Below 1280px the row uses `flex-wrap` and naturally wraps to 3 rows — only relevant during dev work in narrow windows.

### CSS rule

```css
@media (max-width: 1599px) { [data-tier="2"] { display: none; } }
@media (max-width: 1919px) { [data-tier="3"] { display: none; } }
```

### Cluster separators

Within a cluster, items are separated by `·` (`<Sep tone="subtle" />`). Between clusters, by `|` (`<Sep />`). Visual hierarchy without extra ink.

### Tap targets

Tunable readouts (radio frequencies, XPDR) wrap in `<TapTarget>` which adds invisible padding to reach 44px hit area while keeping the visible text inline at strip height.

---

## Viewport scaling

Three coordinated changes fix the "needs 80% zoom" problem:

1. **Strip height drop** — 96 → 64px.
2. **Global rem scale** — `html { font-size: clamp(15px, 0.95vw, 17px) }` lets the layout breathe at 1920px and stay readable at 1366px without breakpoint code.
3. **Dynamic viewport units** — replace `100vh`/`100vw` in `App.jsx` with `100dvh`/`100dvw` so the layout reacts cleanly to F11 fullscreen and to mobile-style chrome show/hide.

After these three: 1920×1080 fullscreen fits at 100% zoom with breathing room. 1366×768 windowed fits without tier-3 items.

---

## ConnectionIndicator (kills "DISC (974)")

New chrome component at `ios/frontend/src/components/ConnectionIndicator.jsx` (not a primitive — too specific). Replaces the bare `DISC ({n})` / `LIVE` text in `ActionBar.jsx:174`.

States:

- **Connected** — solid green dot + `LIVE`. Tooltip: "Backend connected. Last message Xs ago."
- **Disconnected** — pulsing red dot + `RECONNECTING…` + small dimmed reconnect-attempt count (e.g. `(974)` in `fg.dim`). Tooltip: "Backend unreachable. Retrying every 1s."

Reads from existing `wsConnected` and `wsReconnectCount` in the store. No new state plumbing.

---

## Files touched

### Created

- `ios/frontend/src/theme/tokens.js`
- `ios/frontend/src/theme/tokens.css`
- `ios/frontend/src/theme/primitives.jsx`
- `ios/frontend/src/theme/index.js`

### Modified

- `ios/frontend/src/App.jsx` — mount `<ThemeStyles />`, switch to `dvh`/`dvw`.
- `ios/frontend/src/components/StatusStrip.jsx` — full rewrite using primitives, 2-row tiered layout.
- `ios/frontend/src/components/ActionBar.jsx` — replace ActionBtn with Button primitive; replace inline DISC text with ConnectionIndicator.
- `ios/frontend/src/components/NavTabs.jsx` — token replacement, Button primitive (variant=ghost).
- `ios/frontend/src/components/SidePanel.jsx` — PanelHeader/PanelContent use tokens.
- `ios/frontend/src/components/panels/PanelUtils.jsx` — becomes a compat shim. Existing exports (`PanelRow`, `SectionHeader`, `FullWidthBtn`) are reimplemented as thin wrappers over the new primitives so all 11 panels continue to import them unchanged.

### Untouched (Spec B will migrate them)

- All files under `src/components/panels/` except `PanelUtils.jsx`.
- All files under `src/components/ui/`.
- All files under `src/components/cockpit/` (if present).

---

## Verification

- `npm run build` succeeds with no warnings.
- 1920×1080 fullscreen at 100% zoom: status strip is 2 rows, all data visible, no scroll, no clipping.
- 1366×768 windowed at 100% zoom: tier-3 items hidden, layout fits, no horizontal scroll.
- Browser zoom 100/110/125% all work without horizontal scroll on the chrome.
- ConnectionIndicator: kill backend → see pulsing red `RECONNECTING…`; restart backend → see solid green `LIVE`.
- All existing panels (AircraftPanel, FailuresPanel, etc.) still render and function (PanelUtils shim works).
- Optional dev route `/dev/primitives` shows every primitive in every variant — manual visual verification.

---

## Risks and accepted tradeoffs

- **Existing panels will look slightly different** because the global rem scale shifts sizes. Accepted — they remain readable and functional; Spec B will polish them deliberately.
- **The compat shim in `PanelUtils.jsx` couples primitive names to legacy ones.** Mitigated by keeping the shim small and obvious; removed when Spec B is complete.
- **`100dvh` is well-supported but newer than `100vh`.** All target browsers (Chrome/Edge ≥108) support it. Not a concern for an internal IOS.

---

## Open questions resolved during brainstorming

- **Status-strip trigger**: viewport width via media queries (option C from brainstorming).
- **Spec scope**: split into Spec A (this) + Spec B (panel migration) — chosen for tighter PRs.
- **Token mechanism**: hybrid — JS object + CSS variables on `:root` for the scale knob.
- **Status-strip rows**: 2 rows always, with tier-based hiding; below 1280px wraps further.
- **Readability**: bumped defaults vs current (xxs 11→12, xs 13→14, sm 14→15, etc.) based on WCAG / HIG / cockpit-display references.

## Open questions for Spec B (do not resolve now)

- Migration order across panels — likely drive by frequency-of-edit.
- Whether to deprecate `WeatherPanel` (v1) when the migration reaches it.
- Whether cockpit pages adopt the same tokens or get a separate scale.

---

## Out of scope (rejected to prevent scope creep)

- Light theme.
- Configurable tokens at runtime (theme switcher).
- Storybook integration. (A throwaway `/dev/primitives` route is enough.)
- TypeScript migration. (Tokens stay JSDoc'd JS.)
- Removing inline `style={{}}` pattern in favor of CSS-in-JS or CSS Modules.
