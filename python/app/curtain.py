from __future__ import annotations

from collections.abc import Callable

from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QColor, QLinearGradient, QPainter, QPen

from .assets import Assets
from .widgets import ease_in_out


class Curtain:
    """Theatre curtain transition used between screens."""

    def __init__(self, assets: Assets) -> None:
        self.assets = assets
        self.active = False
        self.state = 0  # 0 idle, 1 closing, 2 holding, 3 opening
        self.progress = 0.0
        self._t = 0.0
        self._hold = 0.0
        self._mid: Callable[[], None] | None = None
        self._close_time = 0.82
        self._open_time = 0.98
        self._mid_done = False

    def start(self, on_mid: Callable[[], None]) -> None:
        self.active = True
        self.state = 1
        self._t = 0.0
        self._hold = 0.0
        self._mid = on_mid
        self._mid_done = False

    def update(self, dt: float) -> None:
        if not self.active:
            return
        if self.state == 1:
            self._t += dt / self._close_time
            if self._t >= 1.0:
                self._t = 1.0
                self.state = 2
                self._hold = 0.0
                if self._mid and not self._mid_done:
                    self._mid_done = True
                    self._mid()
        elif self.state == 2:
            self._hold += dt
            if self._hold >= 0.18:
                self.state = 3
        elif self.state == 3:
            self._t -= dt / self._open_time
            if self._t <= 0.0:
                self._t = 0.0
                self.active = False
                self.state = 0
                self._mid = None
        self.progress = self._t

    def paint(self, p: QPainter, w: int = 1920, h: int = 1080) -> None:
        if not self.active:
            return
        e = ease_in_out(self.progress)
        p.save()
        # darken behind, then an opaque deep-red stage so folds or gaps can
        # never reveal the screen underneath
        p.fillRect(QRectF(0, 0, w, h), QColor(0, 0, 0, int(105 * e)))
        p.fillRect(QRectF(0, 0, w, h), QColor(20, 5, 8, int(242 * e)))
        panel = self.assets.ui("PZImgCommonCurtainHorizon.png", int(w * 0.66), h, "stretch")
        if not panel.isNull():
            # each side travels past the centre; the panels overlap heavily
            # and still cover the outer screen edges
            pw = w * 0.64
            x = -pw + e * (pw)
            p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
            p.drawPixmap(QRectF(x, 0, pw, h), panel, QRectF(panel.rect()))
            p.save()
            p.translate(w, 0)
            p.scale(-1, 1)
            p.drawPixmap(QRectF(x, 0, pw, h), panel, QRectF(panel.rect()))
            p.restore()
        valance = self.assets.ui("PZImgLobbyCurtain.png", w, 150, "stretch")
        if not valance.isNull():
            vy = -150 + e * 150
            p.drawPixmap(QRectF(0, vy, w, 150), valance, QRectF(valance.rect()))
        p.restore()
