from __future__ import annotations

import math
import random

import numpy as np
from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QBrush, QColor, QLinearGradient, QPainter, QPen, QRadialGradient


class Spectrum:
    def __init__(self) -> None:
        self.bars = np.zeros(48, dtype=np.float32)
        self.bass = 0.0
        self.energy = 0.0
        self.t = 0.0
        self._rnd = random.Random(8)
        self.particles = [
            [self._rnd.uniform(0, 1920), self._rnd.uniform(500, 1080),
             self._rnd.uniform(4, 16), self._rnd.choice([1, 1, 2, 2, 3]),
             self._rnd.uniform(20, 90), self._rnd.uniform(0, 6.28)]
            for _ in range(64)
        ]

    def update(self, dt: float, ms: int, engine, active: bool = True) -> None:
        self.t += dt
        target, bass, energy = engine.spectrum_at(ms) if active else (
            np.zeros(48, dtype=np.float32), 0.0, 0.0)
        attack = 1.0 - math.exp(-dt / 0.045)
        release = 1.0 - math.exp(-dt / 0.16)
        for i in range(48):
            v = float(target[i]) if i < len(target) else 0.0
            k = attack if v > self.bars[i] else release
            self.bars[i] += (v - self.bars[i]) * k
        kb = 1.0 - math.exp(-dt / 0.09)
        self.bass += (bass - self.bass) * kb
        self.energy += (energy - self.energy) * kb
        for p in self.particles:
            p[1] -= p[2] * dt * (0.55 + self.energy * 1.4)
            p[0] += math.sin(self.t * 0.35 + p[5]) * 11 * dt
            if p[1] < 360:
                p[0] = self._rnd.uniform(0, 1920)
                p[1] = 1080 - self._rnd.uniform(10, 80)
                p[2] = self._rnd.uniform(4, 16)
                p[4] = self._rnd.uniform(20, 90)

    # ------------------------------------------------------------------
    def draw_footlights(self, p: QPainter, w: int = 1920, h: int = 1080, alpha: float = 1.0,
                        lift: float = 0.0) -> None:
        if alpha <= 0.01:
            return
        p.save()
        p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        p.setCompositionMode(QPainter.CompositionMode.CompositionMode_Plus)

        # floor glow
        g = QLinearGradient(0, h - 210 - lift, 0, h - lift)
        g.setColorAt(0.0, QColor(220, 150, 66, 0))
        g.setColorAt(0.72, QColor(232, 162, 78, int(26 * alpha)))
        g.setColorAt(1.0, QColor(252, 206, 130, int(52 * alpha)))
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QBrush(g))
        p.drawRect(QRectF(0, h - 210 - lift, w, 210))

        n = 48
        margin = 18
        span = w - margin * 2
        step = span / (n - 1)
        base_y = h - 2 - lift
        for i, v in enumerate(self.bars):
            vv = float(v) ** 1.18
            flicker = 0.94 + 0.06 * math.sin(self.t * 19 + i * 1.7)
            bh = (14 + 150 * vv) * flicker
            x = margin + i * step
            # outer glow
            for wid, a in ((22, 10), (14, 18), (8, 36), (4, 90)):
                col = QColor(246, 185, 94, int(a * alpha))
                p.setPen(QPen(col, wid, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
                p.drawLine(QPointF(x, base_y), QPointF(x, base_y - bh))
            # hot core
            core = QColor(255, 236, 193, int(205 * alpha))
            p.setPen(QPen(core, 1.6, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
            p.drawLine(QPointF(x, base_y), QPointF(x, base_y - bh + 5))
            # lamp at the floor
            r = 3.0 + 4.0 * vv
            rg = QRadialGradient(QPointF(x, base_y - 2), r * 3)
            rg.setColorAt(0, QColor(255, 232, 180, int(180 * alpha)))
            rg.setColorAt(0.35, QColor(244, 174, 82, int(70 * alpha)))
            rg.setColorAt(1, QColor(244, 174, 82, 0))
            p.setBrush(QBrush(rg))
            p.setPen(Qt.PenStyle.NoPen)
            p.drawEllipse(QPointF(x, base_y - 2), r * 3, r * 3)

        # dust
        for x, y, spd, r, a, ph in self.particles:
            aa = int(a * (0.45 + self.energy * 0.9) * alpha)
            if aa <= 0:
                continue
            p.setBrush(QColor(246, 210, 150, min(150, aa)))
            p.setPen(Qt.PenStyle.NoPen)
            p.drawEllipse(QPointF(x, y), r, r)
            p.setBrush(QColor(246, 210, 150, min(32, aa // 4)))
            p.drawEllipse(QPointF(x, y), r * 3.2, r * 3.2)
        p.restore()

    # ------------------------------------------------------------------
    def draw_halo(self, p: QPainter, cx: float, cy: float, radius: float,
                  alpha: float = 1.0, color: QColor | None = None) -> None:
        if alpha <= 0.01:
            return
        base = color or QColor(226, 180, 112)
        p.save()
        p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        p.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)
        p.translate(cx, cy)
        p.rotate(self.t * 3.0)
        n = 72
        for i in range(n):
            v = float(self.bars[i % len(self.bars)])
            if i % 2:
                v *= 0.62
            length = 22 + 92 * (v ** 1.25)
            ang = 2 * math.pi * i / n
            p.save()
            p.rotate(math.degrees(ang))
            p.setPen(QPen(QColor(12, 8, 5, int(48 * alpha)), 11,
                          Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
            p.drawLine(QPointF(0, -radius), QPointF(0, -radius - length))
            for wid, a in ((10, 36), (6, 82), (2.8, 235)):
                c = QColor(base)
                c.setAlpha(int(a * min(1.0, alpha * 1.35)))
                p.setPen(QPen(c, wid, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
                p.drawLine(QPointF(0, -radius), QPointF(0, -radius - length))
            p.restore()
        p.setPen(QPen(QColor(base.red(), base.green(), base.blue(), int(95 * alpha)), 1.4))
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawEllipse(QPointF(0, 0), radius - 4, radius - 4)
        p.restore()
