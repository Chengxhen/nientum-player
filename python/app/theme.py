from __future__ import annotations

from PySide6.QtCore import QRectF, QPointF, Qt
from PySide6.QtGui import QColor, QFont, QFontDatabase, QFontMetricsF, QPainter, QPen

from . import paths

# ----------------------------------------------------------------------------
# palette
# ----------------------------------------------------------------------------
INK = QColor("#30241f")
INK_SOFT = QColor("#5b4b42")
PAPER = QColor("#e6ddd0")
PAPER_DARK = QColor("#cfc4b4")
CREAM = QColor("#f4ecdf")
GOLD = QColor("#c8a166")
GOLD_LIGHT = QColor("#ecd6a9")
GOLD_DEEP = QColor("#9a713a")
RED = QColor("#6b1d1f")
RED_DARK = QColor("#2a0d0e")
DARK = QColor("#0c0a09")
BLACK = QColor("#000000")
MUTED = QColor("#9d8d7b")
GREEN = QColor("#7b9a72")

REGULAR = QFont.Weight.Normal
MEDIUM = QFont.Weight.Medium
BOLD = QFont.Weight.Bold


class Theme:
    def __init__(self) -> None:
        self.serif_family = "Noto Serif SC"
        self.sans_family = "Noto Sans SC"
        for fname, attr in (("NotoSerifSC-VF.ttf", "serif_family"), ("NotoSansSC-VF.ttf", "sans_family")):
            p = paths.data("fonts", fname)
            if p.exists():
                fid = QFontDatabase.addApplicationFont(str(p))
                fams = QFontDatabase.applicationFontFamilies(fid) if fid >= 0 else []
                if fams:
                    setattr(self, attr, fams[0])
        self._cache: dict[tuple, QFont] = {}
        self._metrics: dict[tuple, QFontMetricsF] = {}

    def metrics(self, font: QFont) -> QFontMetricsF:
        key = (font.family(), font.pixelSize(), font.weight(),
               font.letterSpacing(), font.wordSpacing())
        fm = self._metrics.get(key)
        if fm is None:
            fm = QFontMetricsF(font)
            self._metrics[key] = fm
        return fm

    def font(self, size: float, serif: bool = True, weight: QFont.Weight = REGULAR,
             spacing: float = 0.0) -> QFont:
        key = (size, serif, weight, spacing)
        f = self._cache.get(key)
        if f is None:
            f = QFont(self.serif_family if serif else self.sans_family)
            f.setPixelSize(max(1, int(round(size))))
            f.setWeight(weight)
            if spacing:
                f.setLetterSpacing(QFont.SpacingType.AbsoluteSpacing, spacing)
            f.setStyleStrategy(QFont.StyleStrategy.PreferAntialias)
            self._cache[key] = f
        return f

    # -- drawing helpers -----------------------------------------------------
    def text(self, p: QPainter, x: float, y: float, text: str, font: QFont,
             color: QColor = CREAM, shadow: bool = False, alpha: int = 255) -> None:
        if not text:
            return
        c = QColor(color)
        if alpha < 255:
            c.setAlpha(alpha)
        if shadow:
            sc = QColor(0, 0, 0, min(190, int(c.alpha() * 0.75)))
            p.setFont(font)
            p.setPen(sc)
            p.drawText(QPointF(x + 1.6, y + 1.8), text)
        p.setFont(font)
        p.setPen(c)
        p.drawText(QPointF(x, y), text)

    def text_center(self, p: QPainter, cx: float, y: float, text: str, font: QFont,
                    color: QColor = CREAM, shadow: bool = False, alpha: int = 255) -> None:
        fm = self.metrics(font)
        self.text(p, cx - fm.horizontalAdvance(text) / 2.0, y, text, font, color, shadow, alpha)

    def text_rect(self, p: QPainter, rect: QRectF, text: str, font: QFont,
                  color: QColor = CREAM, align=Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignVCenter,
                  elide: Qt.TextElideMode | None = Qt.TextElideMode.ElideRight) -> None:
        p.setFont(font)
        p.setPen(color)
        fm = self.metrics(font)
        t = fm.elidedText(text, elide, rect.width()) if elide is not None else text
        p.drawText(rect, int(align), t)

    def width(self, text: str, font: QFont) -> float:
        return self.metrics(font).horizontalAdvance(text)


def rounded(p: QPainter, rect: QRectF, radius: float, fill: QColor | None,
            border: QColor | None = None, border_w: float = 1.0) -> None:
    p.setPen(QPen(border, border_w) if border else Qt.PenStyle.NoPen)
    p.setBrush(fill if fill else Qt.BrushStyle.NoBrush)
    p.drawRoundedRect(rect, radius, radius)
