#!/usr/bin/env python3
"""
Generate an engineering-style electrical single-line diagram (SLD) from YAML.

Produces clean SVG with proper electrical symbols:
  - Bus bars as thick horizontal lines
  - Generator/battery symbols for sources
  - Inline switch symbols (N/O, N/C, contactor, relay)
  - Circuit breaker symbols on load connections
  - Motor symbols for motor loads

Usage:
    python3 tools/electrical_diagram/generate_sld.py src/aircraft/c172/config/electrical.yaml
    python3 tools/electrical_diagram/generate_sld.py src/aircraft/ec135/config/electrical.yaml -o output/ec135
"""

import argparse
import sys
from pathlib import Path
from collections import defaultdict

import yaml


# ── Layout constants ─────────────────────────────────────────────────────────

MARGIN = 50
TITLE_H = 55
SOURCE_W = 90
SOURCE_H = 70
SOURCE_GAP = 30
SWITCH_ZONE = 70
BUS_BAR_H = 7
BUS_GAP = 50
TIER_GAP = 170
LOAD_W = 78
LOAD_H = 48
LOAD_GAP = 12
CB_OFFSET = 0.30        # fraction of drop where CB sits
MIN_BUS_W = 140
LEGEND_W = 185
LEGEND_GAP = 30


# ── SVG builder ──────────────────────────────────────────────────────────────

class SVG:
    """Minimal SVG string builder."""

    def __init__(self):
        self.parts: list[str] = []

    def add(self, s: str):
        self.parts.append(s)

    def line(self, x1, y1, x2, y2, **kw):
        attrs = self._attrs(kw)
        self.add(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" {attrs}/>')

    def rect(self, x, y, w, h, **kw):
        attrs = self._attrs(kw)
        self.add(f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" {attrs}/>')

    def circle(self, cx, cy, r, **kw):
        attrs = self._attrs(kw)
        self.add(f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" {attrs}/>')

    def text(self, x, y, content, **kw):
        attrs = self._attrs(kw)
        self.add(f'<text x="{x:.1f}" y="{y:.1f}" {attrs}>{content}</text>')

    def polyline(self, points, **kw):
        pts = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
        attrs = self._attrs(kw)
        self.add(f'<polyline points="{pts}" {attrs}/>')

    @staticmethod
    def _attrs(kw: dict) -> str:
        mapping = {
            "font_family": "font-family",
            "font_size": "font-size",
            "font_weight": "font-weight",
            "text_anchor": "text-anchor",
            "stroke_width": "stroke-width",
            "stroke_dasharray": "stroke-dasharray",
            "fill_opacity": "fill-opacity",
            "stroke_opacity": "stroke-opacity",
            "dominant_baseline": "dominant-baseline",
        }
        parts = []
        for k, v in kw.items():
            attr = mapping.get(k, k)
            parts.append(f'{attr}="{v}"')
        return " ".join(parts)

    def wrap(self, w, h) -> str:
        inner = "\n".join(self.parts)
        return (
            f'<svg xmlns="http://www.w3.org/2000/svg" '
            f'viewBox="0 0 {w:.0f} {h:.0f}" '
            f'width="{w:.0f}" height="{h:.0f}">\n'
            f'<style>\n'
            f'  .label {{ font-family: Helvetica,Arial,sans-serif; }}\n'
            f'  .mono  {{ font-family: "Courier New",Courier,monospace; }}\n'
            f'</style>\n'
            f'{inner}\n'
            f'</svg>'
        )


# ── Diagram generator ────────────────────────────────────────────────────────

class ElectricalSLD:
    """Reads YAML config and produces an engineering-style SVG."""

    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.aircraft = cfg.get("aircraft", {})
        self.sources = {s["id"]: s for s in cfg.get("sources", [])}
        self.buses = {b["id"]: b for b in cfg.get("buses", [])}
        self.bus_order = [b["id"] for b in cfg.get("buses", [])]
        self.switches = cfg.get("switches", [])
        self.loads_list = cfg.get("loads", [])
        self.direct_conns = cfg.get("direct_connections", [])
        self.cas_msgs = cfg.get("cas_messages", [])

        self.source_ids = set(self.sources)
        self.bus_ids = set(self.buses)

        self.loads_by_bus: dict[str, list] = defaultdict(list)
        for ld in self.loads_list:
            self.loads_by_bus[ld["bus"]].append(ld)

        # Computed layout
        self.bus_tiers: dict[str, int] = {}
        self.tier_buses: dict[int, list[str]] = defaultdict(list)
        self.bus_rect: dict[str, tuple] = {}   # id → (x, y, w)
        self.src_pos: dict[str, tuple] = {}    # id → (cx, cy)
        self.load_pos: dict[str, tuple] = {}   # id → (cx, cy)
        self.page_w = 0
        self.page_h = 0
        self.svg = SVG()

    # ── topology analysis ────────────────────────────────────────────────

    def _assign_tiers(self):
        """Topological sort: tier 0 = fed by sources, tier N+1 = fed by tier N buses."""
        tier0 = set()
        for sw in self.switches:
            if sw["source"] in self.source_ids and sw["target"] in self.bus_ids:
                tier0.add(sw["target"])
        for dc in self.direct_conns:
            if dc["from"] in self.source_ids and dc["to"] in self.bus_ids:
                tier0.add(dc["to"])

        assigned = {b: 0 for b in tier0}
        changed = True
        while changed:
            changed = False
            for sw in self.switches:
                s, t = sw["source"], sw["target"]
                if s in assigned and t in self.bus_ids and t not in assigned:
                    assigned[t] = assigned[s] + 1
                    changed = True

        for bid in self.bus_ids:
            if bid not in assigned:
                assigned[bid] = 0

        self.bus_tiers = assigned
        for bid, tier in assigned.items():
            self.tier_buses[tier].append(bid)
        for tier in self.tier_buses:
            self.tier_buses[tier].sort(key=lambda b: self.bus_order.index(b))

    # ── layout computation ───────────────────────────────────────────────

    def _compute_layout(self):
        self._assign_tiers()
        n_tiers = max(self.tier_buses, default=0) + 1

        # Bus widths (driven by number of loads)
        bus_w: dict[str, float] = {}
        for bid in self.bus_ids:
            n = len(self.loads_by_bus.get(bid, []))
            lbl_w = len(self.buses[bid].get("label", bid)) * 7.5 + 80
            loads_w = n * (LOAD_W + LOAD_GAP) + LOAD_GAP if n else 0
            bus_w[bid] = max(MIN_BUS_W, loads_w, lbl_w)

        # Tier Y positions
        tier_y: dict[int, float] = {}
        y0 = TITLE_H + MARGIN + SOURCE_H + SWITCH_ZONE + 10
        for t in range(n_tiers):
            tier_y[t] = y0
            y0 += BUS_BAR_H + TIER_GAP

        # Bus X positions (left-to-right within tier)
        # First pass: compute total width per tier
        tier_widths: dict[int, float] = {}
        for t, blist in self.tier_buses.items():
            tier_widths[t] = sum(bus_w[b] for b in blist) + BUS_GAP * max(0, len(blist) - 1)

        content_w = max(tier_widths.values(), default=400)
        self.page_w = content_w + MARGIN * 2 + LEGEND_W + LEGEND_GAP

        for t, blist in self.tier_buses.items():
            tw = tier_widths[t]
            x = MARGIN + (content_w - tw) / 2  # centre tiers
            for bid in blist:
                self.bus_rect[bid] = (x, tier_y[t], bus_w[bid])
                x += bus_w[bid] + BUS_GAP

        # Source positions — group by target tier-0 bus
        src_bus_map: dict[str, str] = {}
        for sw in self.switches:
            if sw["source"] in self.source_ids:
                src_bus_map.setdefault(sw["source"], sw["target"])
        for dc in self.direct_conns:
            if dc["from"] in self.source_ids:
                src_bus_map.setdefault(dc["from"], dc["to"])

        bus_srcs: dict[str, list[str]] = defaultdict(list)
        for sid, bid in src_bus_map.items():
            bus_srcs[bid].append(sid)

        src_y = TITLE_H + MARGIN + SOURCE_H / 2
        for bid, slist in bus_srcs.items():
            if bid not in self.bus_rect:
                continue
            bx, _, bw = self.bus_rect[bid]
            n = len(slist)
            tw = n * SOURCE_W + (n - 1) * SOURCE_GAP
            sx = bx + (bw - tw) / 2
            for i, sid in enumerate(slist):
                cx = sx + i * (SOURCE_W + SOURCE_GAP) + SOURCE_W / 2
                self.src_pos[sid] = (cx, src_y)

        for sid in self.source_ids:
            if sid not in self.src_pos:
                self.src_pos[sid] = (self.page_w - LEGEND_W - MARGIN - SOURCE_W / 2, src_y)

        # Load positions (below their bus)
        for bid in self.bus_ids:
            loads = self.loads_by_bus.get(bid, [])
            if not loads:
                continue
            bx, by, bw = self.bus_rect[bid]
            n = len(loads)
            tw = n * (LOAD_W + LOAD_GAP) - LOAD_GAP
            sx = bx + (bw - tw) / 2
            ly = by + BUS_BAR_H + 70
            for i, ld in enumerate(loads):
                cx = sx + i * (LOAD_W + LOAD_GAP) + LOAD_W / 2
                self.load_pos[ld["id"]] = (cx, ly)

        # Page height
        max_ly = max((p[1] for p in self.load_pos.values()), default=y0)
        self.page_h = max_ly + LOAD_H / 2 + MARGIN + 10

    # ── drawing helpers ──────────────────────────────────────────────────

    def _src_colour(self, stype: str):
        if "battery" in stype:
            return ("#FFF8E1", "#F9A825")
        if "external" in stype:
            return ("#E3F2FD", "#1565C0")
        if "apu" in stype:
            return ("#F1F8E9", "#558B2F")
        return ("#E8F5E9", "#2E7D32")

    def _draw_source(self, sid: str):
        s = self.sources[sid]
        cx, cy = self.src_pos[sid]
        x, y = cx - SOURCE_W / 2, cy - SOURCE_H / 2
        fill, stroke = self._src_colour(s["type"])

        self.svg.rect(x, y, SOURCE_W, SOURCE_H, rx="5", fill=fill,
                      stroke=stroke, stroke_width="2")

        stype = s["type"]
        if "battery" in stype:
            # battery symbol
            bx, by_top = cx, cy - 14
            self.svg.line(bx - 14, by_top, bx + 14, by_top, stroke=stroke, stroke_width="3.5")
            self.svg.line(bx - 8, by_top + 7, bx + 8, by_top + 7, stroke=stroke, stroke_width="3.5")
            self.svg.text(cx, cy + 4, f'{s["battery"]["capacity_ah"]}Ah',
                          **self._mono(8), text_anchor="middle", fill="#444")
        else:
            # generator circle
            self.svg.circle(cx, cy - 8, 15, fill="none", stroke=stroke, stroke_width="2")
            sym = "S/G" if "starter" in stype else "G"
            fs = 9 if len(sym) <= 2 else 8
            self.svg.text(cx, cy - 4, sym,
                          **self._mono(fs, bold=True), text_anchor="middle", fill=stroke)

        # label + specs
        self.svg.text(cx, cy + 18, s.get("label", sid),
                      **self._label(8, bold=True), text_anchor="middle")
        info = f'{s["nominal_voltage"]}V  {s.get("max_current", "?")}A'
        self.svg.text(cx, cy + 29, info,
                      **self._mono(7), text_anchor="middle", fill="#666")

    def _draw_bus(self, bid: str):
        b = self.buses[bid]
        bx, by, bw = self.bus_rect[bid]
        shed = b.get("shed_priority", 0)

        if shed == -1:
            fill, lbl_col = "#C62828", "#C62828"
        elif shed <= 1:
            fill, lbl_col = "#E65100", "#E65100"
        else:
            fill, lbl_col = "#37474F", "#37474F"

        self.svg.rect(bx, by, bw, BUS_BAR_H, fill=fill, rx="1")

        label = b.get("label", bid)
        shed_str = "ESSENTIAL" if shed == -1 else f"shed P{shed}"
        vtype = b["type"].upper()
        info = f"{label}    {b['nominal_voltage']}V {vtype}  [{shed_str}]"
        self.svg.text(bx + bw / 2, by - 6, info,
                      **self._label(8, bold=True), text_anchor="middle", fill=lbl_col)

    def _draw_switch_inline(self, x: float, y_top: float, y_bot: float, sw: dict):
        """Draw switch symbol on a vertical line between y_top and y_bot."""
        mid = (y_top + y_bot) / 2
        closed = sw.get("default_closed", False)
        stype = sw.get("type", "switch")
        label = sw.get("label", sw["id"])
        pilot = "P" if sw.get("pilot_controllable") else "A"

        # wire above / below switch
        self.svg.line(x, y_top, x, mid - 11, stroke="#333", stroke_width="1.5")
        self.svg.line(x, mid + 11, x, y_bot, stroke="#333", stroke_width="1.5")

        # contact symbol
        if closed:
            self.svg.circle(x, mid + 9, 2.5, fill="#333", stroke="none")
            self.svg.circle(x, mid - 9, 2.5, fill="#333", stroke="none")
            self.svg.line(x, mid + 7, x, mid - 7, stroke="#333", stroke_width="2")
        else:
            self.svg.circle(x, mid + 9, 2.5, fill="#333", stroke="none")
            self.svg.circle(x, mid - 9, 2.5, fill="none", stroke="#333", stroke_width="1.5")
            self.svg.line(x, mid + 7, x + 9, mid - 9, stroke="#333", stroke_width="2")

        # type indicator
        if stype in ("contactor", "relay"):
            coil_x = x + 15
            self.svg.rect(coil_x, mid - 5, 10, 10, fill="none", stroke="#333", stroke_width="1")
            self.svg.line(x + 3, mid, coil_x, mid,
                          stroke="#333", stroke_width="0.8", stroke_dasharray="2,1")
            if stype == "relay" and sw.get("coil_bus"):
                self.svg.text(coil_x + 13, mid + 3, sw["coil_bus"],
                              **self._mono(5), fill="#999")
        elif stype == "bus_tie":
            self.svg.line(x - 5, mid - 1.5, x + 5, mid - 1.5, stroke="#333", stroke_width="2")
            self.svg.line(x - 5, mid + 1.5, x + 5, mid + 1.5, stroke="#333", stroke_width="2")

        # label
        lx = x + (28 if stype in ("contactor", "relay") else 12)
        self.svg.text(lx, mid - 4, f"{label}", **self._label(7), fill="#444")
        self.svg.text(lx, mid + 5, f"[{pilot}]", **self._mono(6), fill="#888")

    def _draw_cb_and_load(self, ld: dict):
        lid = ld["id"]
        if lid not in self.load_pos:
            return
        cx, cy = self.load_pos[lid]
        bx, by, bw = self.bus_rect[ld["bus"]]
        y_bus_bot = by + BUS_BAR_H
        y_load_top = cy - LOAD_H / 2

        # vertical wire
        self.svg.line(cx, y_bus_bot, cx, y_load_top,
                      stroke="#666", stroke_width="1")

        # CB symbol
        cb = ld.get("circuit_breaker", {})
        if cb:
            cb_y = y_bus_bot + (y_load_top - y_bus_bot) * CB_OFFSET
            r = 5
            self.svg.circle(cx, cb_y, r, fill="white", stroke="#C62828", stroke_width="1.4")
            self.svg.line(cx - 3, cb_y - 3, cx + 3, cb_y + 3, stroke="#C62828", stroke_width="1.2")
            self.svg.line(cx - 3, cb_y + 3, cx + 3, cb_y - 3, stroke="#C62828", stroke_width="1.2")
            self.svg.text(cx + 8, cb_y + 3, f'{cb["rating"]}A',
                          **self._mono(6), fill="#C62828")

        # load box
        lx = cx - LOAD_W / 2
        ly = cy - LOAD_H / 2
        essential = ld.get("essential", False)
        ltype = ld.get("type", "")

        if ltype == "motor":
            fill, stroke = "#E8F5E9", "#2E7D32"
        elif essential:
            fill, stroke = "#FFEBEE", "#C62828"
        elif ltype == "resistive":
            fill, stroke = "#FFF3E0", "#E65100"
        else:
            fill, stroke = "#FAFAFA", "#616161"

        self.svg.rect(lx, ly, LOAD_W, LOAD_H, rx="3",
                      fill=fill, stroke=stroke, stroke_width="1.2")

        # motor M-in-circle
        if ltype == "motor":
            self.svg.circle(cx, cy - 8, 9, fill="none", stroke=stroke, stroke_width="1.2")
            self.svg.text(cx, cy - 5, "M",
                          **self._label(9, bold=True), text_anchor="middle", fill=stroke)
            text_y = cy + 7
        elif ltype == "resistive":
            # resistor zigzag hint
            zx = cx - 10
            zy = cy - 10
            pts = [(zx, zy), (zx + 5, zy - 4), (zx + 10, zy + 4),
                   (zx + 15, zy - 4), (zx + 20, zy + 4)]
            self.svg.polyline(pts, fill="none", stroke=stroke, stroke_width="1")
            text_y = cy + 4
        else:
            text_y = cy - 4

        self.svg.text(cx, text_y, ld.get("label", lid),
                      **self._label(7, bold=True), text_anchor="middle")
        self.svg.text(cx, text_y + 11, f'{ld["nominal_current"]}A',
                      **self._mono(6), text_anchor="middle", fill="#666")
        # affected systems
        affected = ld.get("affected_systems", [])
        if affected:
            self.svg.text(cx, text_y + 20, ", ".join(affected),
                          **self._mono(5), text_anchor="middle", fill="#999")

    # ── connections ──────────────────────────────────────────────────────

    def _draw_connections(self):
        # Source → bus via switch
        for sw in self.switches:
            s, t = sw["source"], sw["target"]
            if s in self.source_ids and t in self.bus_ids:
                scx, scy = self.src_pos[s]
                bx, by, bw = self.bus_rect[t]
                y_src_bot = scy + SOURCE_H / 2
                connect_x = max(bx + 8, min(scx, bx + bw - 8))
                self._draw_switch_inline(connect_x, y_src_bot, by, sw)

            elif s in self.bus_ids and t in self.bus_ids:
                self._draw_bus_to_bus(sw)

        # Direct connections
        for dc in self.direct_conns:
            fr, to = dc["from"], dc["to"]
            if fr in self.source_ids and to in self.bus_ids:
                scx, scy = self.src_pos[fr]
                bx, by, bw = self.bus_rect[to]
                y_src_bot = scy + SOURCE_H / 2
                tx = max(bx + 8, min(scx, bx + bw - 8))
                self.svg.line(scx, y_src_bot, tx, by,
                              stroke="#E65100", stroke_width="2.5",
                              stroke_dasharray="6,3")
                mx, my = (scx + tx) / 2, (y_src_bot + by) / 2
                self.svg.text(mx + 8, my, "DIRECT",
                              **self._label(7, bold=True), fill="#E65100")

    def _draw_bus_to_bus(self, sw: dict):
        sb = self.bus_rect[sw["source"]]
        tb = self.bus_rect[sw["target"]]
        st = self.bus_tiers[sw["source"]]
        tt = self.bus_tiers[sw["target"]]

        if st < tt:
            # vertical drop — route from source bus bottom to target bus top
            s_bx, s_by, s_bw = sb
            t_bx, t_by, t_bw = tb
            # pick connection X near target bus centre, clamped to source bus extent
            t_mid = t_bx + t_bw / 2
            sx = max(s_bx + 8, min(t_mid, s_bx + s_bw - 8))
            tx = max(t_bx + 8, min(sx, t_bx + t_bw - 8))

            s_bot = s_by + BUS_BAR_H
            t_top = t_by

            if abs(sx - tx) < 6:
                self._draw_switch_inline(tx, s_bot, t_top, sw)
            else:
                mid_y = (s_bot + t_top) / 2
                avg_x = (sx + tx) / 2
                self.svg.line(sx, s_bot, sx, mid_y - 18,
                              stroke="#333", stroke_width="1.5")
                self.svg.line(sx, mid_y - 18, avg_x, mid_y - 18,
                              stroke="#333", stroke_width="1.5")
                self._draw_switch_inline(avg_x, mid_y - 18, mid_y + 18, sw)
                self.svg.line(avg_x, mid_y + 18, tx, mid_y + 18,
                              stroke="#333", stroke_width="1.5")
                self.svg.line(tx, mid_y + 18, tx, t_top,
                              stroke="#333", stroke_width="1.5")
        else:
            # same tier — horizontal jumper between adjacent buses
            s_bx, s_by, s_bw = sb
            t_bx, t_by, t_bw = tb
            y = s_by + BUS_BAR_H / 2
            x1 = s_bx + s_bw
            x2 = t_bx
            if x1 > x2:
                x1, x2 = t_bx + t_bw, s_bx
            self.svg.line(x1, y, x2, y, stroke="#333", stroke_width="1.5")
            mx = (x1 + x2) / 2
            label = sw.get("label", sw["id"])
            self.svg.text(mx, y - 6, label, **self._label(7), text_anchor="middle", fill="#555")

    # ── legend + CAS ─────────────────────────────────────────────────────

    def _draw_legend(self):
        lx = self.page_w - LEGEND_W - MARGIN / 2
        ly = TITLE_H + MARGIN
        lh = 260
        self.svg.rect(lx, ly, LEGEND_W, lh, rx="4",
                      fill="#F5F5F5", stroke="#BDBDBD", stroke_width="1")
        self.svg.text(lx + LEGEND_W / 2, ly + 16, "LEGEND",
                      **self._label(10, bold=True), text_anchor="middle")

        y = ly + 36
        sx = lx + 22

        # N/O
        self.svg.circle(sx, y, 2.5, fill="#333", stroke="none")
        self.svg.circle(sx, y - 12, 2.5, fill="none", stroke="#333", stroke_width="1.3")
        self.svg.line(sx, y - 2, sx + 8, y - 12, stroke="#333", stroke_width="1.8")
        self.svg.text(sx + 18, y - 4, "Normally Open switch",
                      **self._label(7), fill="#333")

        y += 24
        self.svg.circle(sx, y, 2.5, fill="#333", stroke="none")
        self.svg.circle(sx, y - 12, 2.5, fill="#333", stroke="none")
        self.svg.line(sx, y - 2, sx, y - 10, stroke="#333", stroke_width="1.8")
        self.svg.text(sx + 18, y - 4, "Normally Closed switch",
                      **self._label(7), fill="#333")

        y += 24
        self.svg.rect(sx - 4, y - 5, 8, 8, fill="none", stroke="#333", stroke_width="1")
        self.svg.text(sx + 18, y + 1, "Contactor / Relay coil",
                      **self._label(7), fill="#333")

        y += 22
        r = 4
        self.svg.circle(sx, y, r, fill="white", stroke="#C62828", stroke_width="1.3")
        self.svg.line(sx - 2, y - 2, sx + 2, y + 2, stroke="#C62828", stroke_width="1")
        self.svg.line(sx - 2, y + 2, sx + 2, y - 2, stroke="#C62828", stroke_width="1")
        self.svg.text(sx + 18, y + 3, "Circuit Breaker",
                      **self._label(7), fill="#333")

        y += 22
        self.svg.line(sx - 8, y, sx + 8, y,
                      stroke="#E65100", stroke_width="2", stroke_dasharray="4,2")
        self.svg.text(sx + 18, y + 3, "Direct connection",
                      **self._label(7), fill="#333")

        y += 22
        self.svg.text(sx - 8, y, "[P] Pilot   [A] Auto",
                      **self._mono(7), fill="#555")

        y += 22
        for color, lbl in [("#37474F", "Normal bus"), ("#C62828", "Essential bus"),
                           ("#E65100", "Low-shed-priority bus")]:
            self.svg.rect(sx - 8, y - 3, 18, BUS_BAR_H - 1, fill=color, rx="1")
            self.svg.text(sx + 18, y + 2, lbl, **self._label(7), fill="#333")
            y += 16

        y += 6
        for fill, stroke, lbl in [("#FFEBEE", "#C62828", "Essential load"),
                                   ("#E8F5E9", "#2E7D32", "Motor load"),
                                   ("#FFF3E0", "#E65100", "Resistive load"),
                                   ("#FAFAFA", "#616161", "Standard load")]:
            self.svg.rect(sx - 8, y - 5, 18, 11, rx="2",
                          fill=fill, stroke=stroke, stroke_width="1")
            self.svg.text(sx + 18, y + 3, lbl, **self._label(7), fill="#333")
            y += 17

    def _draw_cas(self):
        if not self.cas_msgs:
            return
        lx = self.page_w - LEGEND_W - MARGIN / 2
        ly = TITLE_H + MARGIN + 270
        h = len(self.cas_msgs) * 14 + 28
        self.svg.rect(lx, ly, LEGEND_W, h, rx="4",
                      fill="#FFF8E1", stroke="#FFB300", stroke_width="1")
        self.svg.text(lx + LEGEND_W / 2, ly + 16, "CAS ALERTS",
                      **self._label(9, bold=True), text_anchor="middle", fill="#E65100")

        y = ly + 32
        for msg in self.cas_msgs:
            lvl = msg.get("level", "")
            col = {"warning": "#C62828", "caution": "#E65100",
                   "advisory": "#1565C0"}.get(lvl, "#333")
            icon = {"warning": "W", "caution": "C", "advisory": "A"}.get(lvl, "?")
            cond = msg.get("condition", {})
            thr = f' &lt;{cond["threshold"]}' if cond.get("threshold") else ""
            detail = f'{cond.get("type", "")} {cond.get("target_id", "")}{thr}'
            self.svg.text(lx + 10, y, f"[{icon}] {msg['text']}",
                          **self._mono(7), fill=col)
            self.svg.text(lx + 14, y + 9, detail, **self._mono(5), fill="#999")
            y += 20

    # ── text style helpers ───────────────────────────────────────────────

    @staticmethod
    def _label(size, bold=False):
        d = {"class": "label", "font_size": str(size)}
        if bold:
            d["font_weight"] = "bold"
        return d

    @staticmethod
    def _mono(size, bold=False):
        d = {"class": "mono", "font_size": str(size)}
        if bold:
            d["font_weight"] = "bold"
        return d

    # ── main entry ───────────────────────────────────────────────────────

    def generate(self) -> str:
        self._compute_layout()

        # background
        self.svg.rect(0, 0, self.page_w, self.page_h, fill="white")
        self.svg.rect(4, 4, self.page_w - 8, self.page_h - 8,
                      fill="none", stroke="#CCC", stroke_width="0.5")

        # title
        title = f"{self.aircraft.get('type', 'Aircraft')} — Electrical Single-Line Diagram"
        sub = f"{self.aircraft.get('designation', '')}   Rev {self.aircraft.get('revision', '?')}"
        cx = (self.page_w - LEGEND_W - LEGEND_GAP) / 2 + MARGIN
        self.svg.text(cx, 26, title,
                      **self._label(15, bold=True), text_anchor="middle")
        self.svg.text(cx, 42, sub,
                      **self._label(10), text_anchor="middle", fill="#666")

        # sources
        for sid in self.sources:
            self._draw_source(sid)

        # buses
        for bid in self.buses:
            self._draw_bus(bid)

        # connections (switches + direct)
        self._draw_connections()

        # loads
        for ld in self.loads_list:
            self._draw_cb_and_load(ld)

        # legend + CAS
        self._draw_legend()
        self._draw_cas()

        return self.svg.wrap(self.page_w, self.page_h)


# ── CLI ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Generate electrical single-line diagram from aircraft YAML config")
    parser.add_argument("yaml_path", help="Path to electrical.yaml")
    parser.add_argument("-o", "--output", help="Output path (without extension)")
    parser.add_argument("-f", "--format", default="svg", choices=["svg", "png", "pdf"],
                        help="Output format (default: svg)")
    args = parser.parse_args()

    path = Path(args.yaml_path)
    if not path.exists():
        print(f"Error: {path} not found", file=sys.stderr)
        sys.exit(1)

    with open(path) as f:
        cfg = yaml.safe_load(f)

    sld = ElectricalSLD(cfg)
    svg_text = sld.generate()

    stem = path.parent.parent.name
    out_base = args.output or f"tools/electrical_diagram/{stem}_electrical_sld"

    svg_path = f"{out_base}.svg"
    Path(svg_path).write_text(svg_text)

    if args.format == "svg":
        print(f"Generated: {svg_path}")
    else:
        import subprocess
        out_path = f"{out_base}.{args.format}"
        fmt_flag = ["-f", "pdf"] if args.format == "pdf" else []
        try:
            subprocess.run(
                ["rsvg-convert", *fmt_flag, "-o", out_path, svg_path],
                check=True)
            print(f"Generated: {out_path}")
        except FileNotFoundError:
            print(f"Generated SVG: {svg_path}  (install librsvg2-bin for {args.format})")


if __name__ == "__main__":
    main()
