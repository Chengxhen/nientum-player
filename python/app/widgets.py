from __future__ import annotations

import math

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import (QBrush, QColor, QFont, QLinearGradient, QPainter, QPainterPath,
                           QPen, QPolygonF)

from .theme import (BLACK, CREAM, DARK, GOLD, GOLD_DEEP, GOLD_LIGHT, INK, PAPER, PAPER_DARK,
                    RED, rounded)


def clamp(v, lo=0.0, hi=1.0):
    return max(lo, min(hi, v))


def lerp(a, b, t):
    return a + (b - a) * t


def ease_out(t: float) -> float:
    t = clamp(t)
    return 1 - (1 - t) ** 3


def ease_in_out(t: float) -> float:
    t = clamp(t)
    return t * t * (3 - 2 * t)


def hit(rect: QRectF, pos: QPointF) -> bool:
    return rect.contains(pos)


def draw_icon(p: QPainter, name: str, r: QRectF, color: QColor = CREAM, width: float = 2.0) -> None:
    p.save()
    p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    pen = QPen(color, width, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin)
    p.setPen(pen)
    p.setBrush(Qt.BrushStyle.NoBrush)
    x, y, w, h = r.x(), r.y(), r.width(), r.height()
    cx, cy = x + w / 2, y + h / 2
    s = min(w, h)
    if name == "play":
        p.setBrush(QBrush(color))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawPolygon(QPolygonF([QPointF(cx - s * 0.20, cy - s * 0.28),
                                 QPointF(cx - s * 0.20, cy + s * 0.28),
                                 QPointF(cx + s * 0.30, cy)]))
    elif name == "pause":
        p.setBrush(QBrush(color))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawRoundedRect(QRectF(cx - s * 0.22, cy - s * 0.28, s * 0.15, s * 0.56), 1.5, 1.5)
        p.drawRoundedRect(QRectF(cx + s * 0.07, cy - s * 0.28, s * 0.15, s * 0.56), 1.5, 1.5)
    elif name in ("prev", "next"):
        p.setBrush(QBrush(color))
        p.setPen(Qt.PenStyle.NoPen)
        d = 1 if name == "next" else -1
        p.drawPolygon(QPolygonF([QPointF(cx - d * s * 0.24, cy - s * 0.26),
                                 QPointF(cx - d * s * 0.24, cy + s * 0.26),
                                 QPointF(cx + d * s * 0.10, cy)]))
        p.drawRoundedRect(QRectF(cx + d * s * 0.16 - s * 0.04, cy - s * 0.28, s * 0.08, s * 0.56), 1, 1)
    elif name in ("volume", "mute"):
        p.setBrush(QBrush(color))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawPolygon(QPolygonF([QPointF(cx - s * 0.36, cy - s * 0.12),
                                 QPointF(cx - s * 0.20, cy - s * 0.12),
                                 QPointF(cx - s * 0.02, cy - s * 0.30),
                                 QPointF(cx - s * 0.02, cy + s * 0.30),
                                 QPointF(cx - s * 0.20, cy + s * 0.12),
                                 QPointF(cx - s * 0.36, cy + s * 0.12)]))
        if name == "mute":
            p.setPen(QPen(color, width, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
            p.drawLine(QPointF(cx + s * 0.06, cy - s * 0.14), QPointF(cx + s * 0.26, cy + s * 0.14))
            p.drawLine(QPointF(cx + s * 0.26, cy - s * 0.14), QPointF(cx + s * 0.06, cy + s * 0.14))
        else:
            p.setPen(QPen(color, width, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
            p.drawArc(QRectF(cx + s * 0.02, cy - s * 0.20, s * 0.30, s * 0.40), -55 * 16, 110 * 16)
            p.drawArc(QRectF(cx + s * 0.02, cy - s * 0.32, s * 0.46, s * 0.64), -50 * 16, 100 * 16)
    elif name == "fullscreen":
        k = s * 0.26
        for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
            px = cx + sx * s * 0.30
            py = cy + sy * s * 0.30
            p.drawLine(QPointF(px, py), QPointF(px - sx * k, py))
            p.drawLine(QPointF(px, py), QPointF(px, py - sy * k))
    elif name == "restore":
        k = s * 0.24
        for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
            px = cx + sx * s * 0.16
            py = cy + sy * s * 0.16
            p.drawLine(QPointF(px, py), QPointF(px + sx * k, py))
            p.drawLine(QPointF(px, py), QPointF(px, py + sy * k))
    elif name == "close":
        p.drawLine(QPointF(cx - s * 0.20, cy - s * 0.20), QPointF(cx + s * 0.20, cy + s * 0.20))
        p.drawLine(QPointF(cx + s * 0.20, cy - s * 0.20), QPointF(cx - s * 0.20, cy + s * 0.20))
    elif name in ("left", "right", "up", "down"):
        p.setBrush(Qt.BrushStyle.NoBrush)
        if name == "left":
            p.drawPolyline(QPolygonF([QPointF(cx + s * 0.12, cy - s * 0.22), QPointF(cx - s * 0.12, cy),
                                      QPointF(cx + s * 0.12, cy + s * 0.22)]))
        elif name == "right":
            p.drawPolyline(QPolygonF([QPointF(cx - s * 0.12, cy - s * 0.22), QPointF(cx + s * 0.12, cy),
                                      QPointF(cx - s * 0.12, cy + s * 0.22)]))
        elif name == "up":
            p.drawPolyline(QPolygonF([QPointF(cx - s * 0.22, cy + s * 0.12), QPointF(cx, cy - s * 0.12),
                                      QPointF(cx + s * 0.22, cy + s * 0.12)]))
        else:
            p.drawPolyline(QPolygonF([QPointF(cx - s * 0.22, cy - s * 0.12), QPointF(cx, cy + s * 0.12),
                                      QPointF(cx + s * 0.22, cy - s * 0.12)]))
    elif name == "back":
        p.drawLine(QPointF(cx + s * 0.22, cy), QPointF(cx - s * 0.18, cy))
        p.drawPolyline(QPolygonF([QPointF(cx + s * 0.02, cy - s * 0.18), QPointF(cx - s * 0.18, cy),
                                  QPointF(cx + s * 0.02, cy + s * 0.18)]))
    elif name == "cog":
        p.drawEllipse(QPointF(cx, cy), s * 0.13, s * 0.13)
        for i in range(8):
            a = i * math.pi / 4
            p.drawLine(QPointF(cx + math.cos(a) * s * 0.20, cy + math.sin(a) * s * 0.20),
                       QPointF(cx + math.cos(a) * s * 0.31, cy + math.sin(a) * s * 0.31))
        p.drawEllipse(QPointF(cx, cy), s * 0.31, s * 0.31)
    elif name == "image":
        p.drawRoundedRect(QRectF(cx - s * 0.30, cy - s * 0.24, s * 0.60, s * 0.48), 2, 2)
        p.drawEllipse(QPointF(cx - s * 0.13, cy - s * 0.09), s * 0.05, s * 0.05)
        p.drawPolyline(QPolygonF([QPointF(cx - s * 0.24, cy + s * 0.18), QPointF(cx - s * 0.04, cy - s * 0.02),
                                  QPointF(cx + s * 0.08, cy + s * 0.10), QPointF(cx + s * 0.18, cy + s * 0.02),
                                  QPointF(cx + s * 0.26, cy + s * 0.18)]))
    elif name == "stop":
        p.setBrush(QBrush(color))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawRoundedRect(QRectF(cx - s * 0.20, cy - s * 0.20, s * 0.40, s * 0.40), 1.6, 1.6)
    p.restore()


def draw_paper_button(p: QPainter, rect: QRectF, label: str, font: QFont,
                      hover: bool = False, primary: bool = False, enabled: bool = True,
                      icon: str | None = None) -> None:
    p.save()
    p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    alpha = 255 if enabled else 90
    shadow = QRectF(rect.x() + 1.5, rect.y() + 3, rect.width(), rect.height())
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QColor(0, 0, 0, 90 if enabled else 30))
    p.drawRoundedRect(shadow, 5, 5)
    if hover and enabled:
        p.setBrush(QColor(246, 202, 132, 30))
        p.drawRoundedRect(rect.adjusted(-4, -3, 4, 5), 8, 8)
    rr = rect.adjusted(0, -1 if hover and enabled else 0, 0, 0)
    if primary:
        g = QLinearGradient(rr.topLeft(), rr.bottomLeft())
        g.setColorAt(0, QColor(230, 202, 150, alpha))
        g.setColorAt(0.52, QColor(202, 166, 108, alpha))
        g.setColorAt(1, QColor(166, 128, 74, alpha))
        p.setBrush(QBrush(g))
        p.setPen(QPen(QColor(248, 230, 194, alpha), 1.2))
        txt = QColor(35, 25, 19, alpha)
    else:
        g = QLinearGradient(rr.topLeft(), rr.bottomLeft())
        g.setColorAt(0, QColor(32, 25, 20, 220 if enabled else 130))
        g.setColorAt(1, QColor(12, 9, 7, 220 if enabled else 130))
        p.setBrush(QBrush(g))
        p.setPen(QPen(QColor(200, 165, 112, int(alpha * (0.95 if hover else 0.60))), 1.1))
        txt = QColor(241, 228, 209, alpha)
    p.drawRoundedRect(rr, 5, 5)
    p.setPen(QPen(QColor(255, 246, 224, 26 if enabled else 10), 1))
    p.drawLine(QPointF(rr.x() + 8, rr.y() + 1.5), QPointF(rr.right() - 8, rr.y() + 1.5))
    if hover and enabled:
        p.setBrush(QColor(255, 236, 196, 16))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawRoundedRect(rr, 5, 5)
    p.setFont(font)
    p.setPen(txt)
    if icon:
        icon_r = QRectF(rect.x() + 14, rect.center().y() - 11, 22, 22)
        draw_icon(p, icon, icon_r, txt, 1.8)
        p.drawText(QRectF(rect.x() + 42, rect.y(), rect.width() - 50, rect.height()),
                   int(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter), label)
    else:
        p.drawText(rect, int(Qt.AlignmentFlag.AlignCenter), label)
    p.restore()


def draw_icon_button(p: QPainter, rect: QRectF, icon: str, hover: bool = False,
                     color: QColor = CREAM, diameter: float | None = None,
                     active: bool = False) -> None:
    p.save()
    p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
    r = rect
    if diameter:
        r = QRectF(rect.center().x() - diameter / 2, rect.center().y() - diameter / 2, diameter, diameter)
    if hover or active:
        p.setBrush(QColor(240, 200, 138, 20))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawEllipse(r.adjusted(-4, -4, 4, 4))
    p.setBrush(QColor(10, 8, 6, 140 if hover else 92))
    p.setPen(QPen(QColor(214, 178, 120, 200 if (hover or active) else 78), 1.1))
    p.drawEllipse(r)
    if hover or active:
        p.setBrush(QColor(240, 200, 138, 16))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawEllipse(r.adjusted(1.5, 1.5, -1.5, -1.5))
    draw_icon(p, icon, r.adjusted(r.width() * 0.235, r.height() * 0.235,
                                 -r.width() * 0.235, -r.height() * 0.235),
              color, 1.95)
    p.restore()


def draw_keycap(p: QPainter, x: float, y: float, label: str, font: QFont,
                h: float = 28) -> float:
    tw = p.fontMetrics().horizontalAdvance(label) if p.font() == font else QFontMetricsF(font).horizontalAdvance(label)
    w = tw + 18
    r = QRectF(x, y, w, h)
    p.setPen(Qt.PenStyle.NoPen)
    p.setBrush(QColor(0, 0, 0, 95))
    p.drawRoundedRect(QRectF(x + 0.6, y + 2, w, h), 4, 4)
    g = QLinearGradient(r.topLeft(), r.bottomLeft())
    g.setColorAt(0, QColor(238, 230, 216, 240))
    g.setColorAt(1, QColor(196, 184, 166, 235))
    p.setBrush(QBrush(g))
    p.setPen(QPen(QColor(128, 108, 86, 210), 1))
    p.drawRoundedRect(r, 4, 4)
    p.setPen(QPen(QColor(255, 250, 238, 80), 1))
    p.drawLine(QPointF(r.x() + 3, r.y() + 1), QPointF(r.right() - 3, r.y() + 1))
    p.setFont(font)
    p.setPen(QColor(32, 24, 20, 248))
    p.drawText(r, int(Qt.AlignmentFlag.AlignCenter), label)
    return w


from PySide6.QtGui import QFontMetricsF  # noqa: E402  (used by draw_keycap)
