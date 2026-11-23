from __future__ import annotations

import math
import time

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QFontMetricsF, QLinearGradient, QPainter, QPen, QRadialGradient

from .. import paths
from ..theme import CREAM, GOLD, MUTED
from ..widgets import clamp, draw_icon, draw_icon_button, ease_out, hit


class GalleryViewer:
    """Full-screen CG / tachie viewer, drawn as a modal overlay."""

    def __init__(self, window, category: str, index: int = 0, items=None, label: str | None = None) -> None:
        self.window = window
        self.category = category
        self.items = list(items) if items is not None else list(window.library.gallery.get(category, []))
        self.index = max(0, min(len(self.items) - 1, int(index)))
        self.label = label or {
            "curtain": "谢幕 CG", "original": "CG 原画", "story": "剧情 CG",
            "epilogue": "尾声", "tachie": "立绘", "diff": "差分",
        }.get(category, "鉴赏")
        self.zoom = 1.0
        self.pan = QPointF(0, 0)
        self._drag = False
        self._drag_start = QPointF()
        self._pan_start = QPointF()
        self.t = 0.0
        self._fade = 0.0
        self._mouse = QPointF(-1, -1)
        self._hover = ""
        self._pix = None
        self._pix_key = ""
        self._prev_pix = None
        self._prev_rect = QRectF()
        self._step_anim = 1.0
        self._step_dir = 0
        self._press_seen = False
        self._immersive = False
        self._immersive_back = False
        self._bar = QRectF(60, 1000, 1800, 48)
        self._prev = QRectF(76, 1006, 44, 36)
        self._next = QRectF(128, 1006, 44, 36)
        self._zoom_out = QRectF(1248, 1006, 34, 36)
        self._src = QRectF(1150, 1006, 90, 36)
        self._zoom_in = QRectF(1374, 1006, 34, 36)
        self._use_bg = QRectF(1420, 1006, 180, 36)
        self._immerse = QRectF(1620, 1006, 190, 36)
        self._immerse_back = QRectF(36, 36, 56, 56)
        self._bg_toast_until = 0.0
        self._load()

    def _load(self) -> None:
        if not self.items:
            self._pix = None
            return
        item = self.items[self.index]
        self._pix = self.window.assets.full(item.path, 3840)
        self._pix_key = item.path
        self.zoom = 1.0
        self.pan = QPointF(0, 0)

    def tick(self, dt: float, now: float) -> None:
        self.t += dt
        self._fade = min(1.0, self._fade + dt / 0.36)
        self._step_anim = min(1.0, self._step_anim + dt / 0.32)

    # ------------------------------------------------------------------
    def animating(self) -> bool:
        return self._fade < 0.999 or self._step_anim < 0.999

    def _content_rect(self) -> QRectF:
        if self.category in ("tachie", "diff"):
            return QRectF(610, 100, 700, 860)
        return QRectF(80, 136, 1760, 820)

    def _fit_rect(self) -> QRectF:
        if not self.items or self._pix is None or self._pix.isNull():
            return QRectF()
        item = self.items[self.index]
        cr = self._content_rect()
        iw, ih = self._pix.width(), self._pix.height()
        s = min(cr.width() / iw, cr.height() / ih)
        w, h = iw * s, ih * s
        return QRectF(cr.center().x() - w / 2, cr.center().y() - h / 2, w, h)

    def _image_rect(self) -> QRectF:
        base = self._fit_rect()
        if base.isNull():
            return base
        w = base.width() * self.zoom
        h = base.height() * self.zoom
        cx = base.center().x() + self.pan.x()
        cy = base.center().y() + self.pan.y()
        return QRectF(cx - w / 2, cy - h / 2, w, h)

    # ------------------------------------------------------------------
    def _immersive_fit(self, pix) -> QRectF:
        if pix is None or pix.isNull():
            return QRectF()
        iw, ih = pix.width(), pix.height()
        s = min(1920.0 / iw, 1080.0 / ih)
        w, h = iw * s, ih * s
        return QRectF(960 - w / 2, 540 - h / 2, w, h)

    def _paint_immersive(self, p: QPainter) -> None:
        theme = self.window.theme
        fade = self._fade
        p.fillRect(QRectF(0, 0, 1920, 1080), QColor(0, 0, 0, int(255 * fade)))
        e = ease_out(self._step_anim)
        if self._prev_pix is not None and not self._prev_pix.isNull() and e < 0.999:
            old = self._immersive_fit(self._prev_pix)
            p.setOpacity(max(0.0, (1.0 - e) * fade))
            p.drawPixmap(old, self._prev_pix, QRectF(self._prev_pix.rect()))
        if self._pix is not None and not self._pix.isNull():
            new = self._immersive_fit(self._pix)
            p.setOpacity(fade)
            p.drawPixmap(new, self._pix, QRectF(self._pix.rect()))
        p.setOpacity(1.0)
        if self._immersive_back:
            draw_icon_button(p, self._immerse_back, "back", self._hover == "exit",
                             QColor(240, 226, 206, 245), 46)
            pill = QRectF(100, 40, 190, 48)
            p.setBrush(QColor(10, 8, 7, int(185 * fade)))
            p.setPen(QPen(QColor(182, 148, 96, int(120 * fade)), 1))
            p.drawRoundedRect(pill, 8, 8)
            theme.text(p, 118, 70, "返回 CG 列表", theme.font(17, False),
                       QColor(236, 224, 206, int(250 * fade)))

    def paint(self, p: QPainter, now: float) -> None:
        if self._immersive:
            self._paint_immersive(p)
            return
        theme = self.window.theme
        fade = self._fade
        p.fillRect(QRectF(0, 0, 1920, 1080), QColor(4, 3, 3, int(238 * fade)))
        # warm vignette, same family as free play
        rg = QRadialGradient(QPointF(960, 520), 1000)
        rg.setColorAt(0, QColor(0, 0, 0, 0))
        rg.setColorAt(1, QColor(0, 0, 0, int(145 * fade)))
        p.setBrush(rg)
        p.setPen(Qt.PenStyle.NoPen)
        p.drawRect(QRectF(0, 0, 1920, 1080))
        gt = QLinearGradient(0, 0, 0, 170)
        gt.setColorAt(0, QColor(4, 3, 3, int(190 * fade)))
        gt.setColorAt(1, QColor(4, 3, 3, 0))
        p.fillRect(QRectF(0, 0, 1920, 170), gt)
        gb = QLinearGradient(0, 900, 0, 1080)
        gb.setColorAt(0, QColor(4, 3, 3, 0))
        gb.setColorAt(1, QColor(4, 3, 3, int(210 * fade)))
        p.fillRect(QRectF(0, 900, 1920, 180), gb)

        if not self.items:
            theme.text_center(p, 960, 540, "没有可显示的内容", theme.font(24, True),
                              QColor(CREAM.red(), CREAM.green(), CREAM.blue(), int(255 * fade)))
            return
        item = self.items[self.index]
        content = self._content_rect()
        fit = self._fit_rect()
        img = self._image_rect()

        p.save()
        p.setClipRect(content)
        if self.category in ("tachie", "diff"):
            g = QRadialGradient(QPointF(content.center().x(), content.y() + 300), 430)
            g.setColorAt(0, QColor(255, 226, 176, int(45 * fade)))
            g.setColorAt(0.6, QColor(180, 130, 80, int(16 * fade)))
            g.setColorAt(1, QColor(0, 0, 0, 0))
            p.setBrush(g)
            p.setPen(Qt.PenStyle.NoPen)
            p.drawEllipse(QPointF(content.center().x(), content.y() + 330), 430, 490)
        else:
            p.setBrush(QColor(12, 9, 8, int(180 * fade)))
            p.setPen(Qt.PenStyle.NoPen)
            p.drawRect(fit.adjusted(-16, -16, 16, 16))
            p.setPen(QPen(QColor(172, 142, 98, int(130 * fade)), 1.2))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRect(fit.adjusted(-14, -14, 14, 14))

        e = ease_out(self._step_anim)
        if e < 0.999 and self._prev_pix is not None and not self._prev_pix.isNull() and not self._prev_rect.isNull():
            dx = self._step_dir * 42.0
            old = self._prev_rect.translated(-dx * e, 0)
            p.setOpacity(max(0.0, (1.0 - e) * fade))
            p.drawPixmap(old, self._prev_pix, QRectF(self._prev_pix.rect()))
            p.setOpacity(fade)
        if self._pix is not None and not self._pix.isNull():
            dx = self._step_dir * 42.0
            new = img.translated(dx * (1.0 - e), 0)
            p.setOpacity(fade)
            p.drawPixmap(new, self._pix, QRectF(self._pix.rect()))
        p.setOpacity(1.0)
        p.restore()

        alpha = int(255 * fade)
        # top-left label and title
        p.setPen(QPen(QColor(198, 156, 96, int(200 * fade)), 1.6))
        p.drawLine(QPointF(64, 44), QPointF(64, 106))
        theme.text(p, 84, 62, self.label, theme.font(15, False, spacing=2.8),
                   QColor(206, 170, 118, alpha))
        title_font = theme.font(30, True)
        title = theme.metrics(title_font).elidedText(item.title, Qt.TextElideMode.ElideRight, 1080)
        theme.text(p, 84, 98, title, title_font, QColor(242, 232, 216, alpha), shadow=True)
        sub = f"{item.act}   ·   {item.w} × {item.h}" if item.act else f"{item.w} × {item.h}"
        theme.text(p, 86, 126, sub, theme.font(14, False), QColor(196, 178, 156, int(alpha * 0.8)))

        # count pill + close
        count = f"{self.index + 1} / {len(self.items)}"
        cf = theme.font(16, False)
        pill = QRectF(1654, 54, 132, 36)
        p.setBrush(QColor(16, 12, 10, int(190 * fade)))
        p.setPen(QPen(QColor(184, 150, 100, int(120 * fade)), 1))
        p.drawRoundedRect(pill, 18, 18)
        theme.text_center(p, pill.center().x(), pill.center().y() + 6, count, cf,
                          QColor(226, 210, 190, alpha))
        draw_icon_button(p, QRectF(1814, 50, 46, 46), "close", self._hover == "close",
                         QColor(236, 222, 202, alpha), 40)

        # bottom control bar
        bar = self._bar
        p.setBrush(QColor(14, 11, 9, int(218 * fade)))
        p.setPen(QPen(QColor(174, 140, 92, int(135 * fade)), 1))
        p.drawRoundedRect(bar, 6, 6)
        p.setPen(QPen(QColor(255, 244, 222, int(20 * fade)), 1))
        p.drawLine(QPointF(bar.x() + 14, bar.y() + 1.5), QPointF(bar.right() - 14, bar.y() + 1.5))
        draw_icon_button(p, self._prev, "left", self._hover == "prev",
                         QColor(236, 222, 202, alpha), 34)
        draw_icon_button(p, self._next, "right", self._hover == "next",
                         QColor(236, 222, 202, alpha), 34)
        p.setPen(QPen(QColor(174, 140, 92, int(75 * fade)), 1))
        p.drawLine(QPointF(188, bar.y() + 9), QPointF(188, bar.bottom() - 9))
        hint = "滚轮缩放  ·  拖动平移  ·  双击复位  ·  ←→ 切换  ·  ESC 关闭  ·  I 无UI"
        theme.text_center(p, 820, bar.y() + 30, hint, theme.font(13, False),
                          QColor(208, 192, 172, int(alpha * 0.85)))
        # source file
        hover = self._hover == "src"
        p.setBrush(QColor(30, 24, 19, int(228 * fade)) if hover else QColor(18, 14, 12, int(175 * fade)))
        p.setPen(QPen(QColor(206, 168, 108, int((210 if hover else 130) * fade)), 1))
        p.drawRoundedRect(self._src, 5, 5)
        theme.text_center(p, self._src.center().x(), self._src.center().y() + 6, "源文件",
                          theme.font(14, False), QColor(238, 224, 204, alpha))
        # zoom controls
        for rect, label, name in ((self._zoom_out, "−", "zoom_out"), (self._zoom_in, "+", "zoom_in")):
            hover = self._hover == name
            p.setBrush(QColor(28, 22, 18, int(225 * fade)) if hover else QColor(18, 14, 12, int(170 * fade)))
            p.setPen(QPen(QColor(206, 168, 108, int((210 if hover else 120) * fade)), 1))
            p.drawRoundedRect(rect, 5, 5)
            theme.text_center(p, rect.center().x(), rect.center().y() + 8, label,
                              theme.font(20, False), QColor(238, 224, 204, alpha))
        zoom_text = f"{int(round(self.zoom * 100))}%"
        theme.text_center(p, 1322, bar.y() + 29, zoom_text, theme.font(14, False),
                          QColor(216, 198, 176, int(alpha * 0.9)))
        cur_bg = self.window.settings.get("title_bg") or ""
        is_bg = cur_bg == item.path
        hover = self._hover == "use_bg"
        p.setBrush(QColor(30, 24, 19, int(228 * fade)) if hover else QColor(18, 14, 12, int(175 * fade)))
        p.setPen(QPen(QColor(206, 168, 108, int((210 if hover else 130) * fade)), 1))
        p.drawRoundedRect(self._use_bg, 5, 5)
        theme.text_center(p, self._use_bg.center().x(), self._use_bg.center().y() + 6,
                          "已是主界面" if is_bg else "设为主界面", theme.font(14, False),
                          QColor(238, 224, 204, alpha if not is_bg else int(alpha * 0.65)))
        hover = self._hover == "immersive"
        p.setBrush(QColor(30, 24, 19, int(228 * fade)) if hover else QColor(18, 14, 12, int(175 * fade)))
        p.setPen(QPen(QColor(206, 168, 108, int((210 if hover else 120) * fade)), 1))
        p.drawRoundedRect(self._immerse, 5, 5)
        draw_icon(p, "fullscreen",
                  QRectF(self._immerse.x() + 10, self._immerse.center().y() - 8, 16, 16),
                  QColor(238, 224, 204, alpha), 1.6)
        theme.text(p, self._immerse.x() + 34, self._immerse.center().y() + 6, "全屏无UI",
                   theme.font(14, False), QColor(238, 224, 204, alpha))
        if item.variant:
            theme.text_center(p, 960, bar.y() - 16, item.variant, theme.font(13, False),
                              QColor(208, 168, 112, alpha))
        if self._bg_toast_until > time.monotonic():
            toast = QRectF(760, 890, 400, 42)
            p.setBrush(QColor(18, 13, 10, 225))
            p.setPen(QPen(QColor(202, 164, 106, 170), 1))
            p.drawRoundedRect(toast, 8, 8)
            theme.text_center(p, toast.center().x(), toast.center().y() + 6, "已设为主界面背景",
                              theme.font(14, False), QColor(238, 222, 200, 245))

    # ------------------------------------------------------------------
    def _zoom_by(self, factor: float) -> None:
        if self.zoom <= 1.001 and factor > 1:
            self.zoom = clamp(factor, 1.0, 4.0)
            self.pan = QPointF(0, 0)
            return
        self.zoom = clamp(self.zoom * factor, 1.0, 4.0)
        if self.zoom <= 1.001:
            self.pan = QPointF(0, 0)

    def mouse_move(self, pos: QPointF) -> None:
        self._mouse = pos
        self._hover = ""
        if self._immersive:
            if self._immersive_back and hit(self._immerse_back.adjusted(-6, -6, 6, 6), pos):
                self._hover = "exit"
            return
        if hit(QRectF(1814, 50, 46, 46).adjusted(-4, -4, 4, 4), pos):
            self._hover = "close"
        elif hit(self._prev.adjusted(-4, -4, 4, 4), pos):
            self._hover = "prev"
        elif hit(self._next.adjusted(-4, -4, 4, 4), pos):
            self._hover = "next"
        elif hit(self._src.adjusted(-4, -4, 4, 4), pos):
            self._hover = "src"
        elif hit(self._zoom_out.adjusted(-4, -4, 4, 4), pos):
            self._hover = "zoom_out"
        elif hit(self._zoom_in.adjusted(-4, -4, 4, 4), pos):
            self._hover = "zoom_in"
        elif hit(self._use_bg.adjusted(-4, -4, 4, 4), pos):
            self._hover = "use_bg"
        elif hit(self._immerse.adjusted(-4, -4, 4, 4), pos):
            self._hover = "immersive"
        if self._drag:
            d = pos - self._drag_start
            self.pan = self._pan_start + d

    def mouse_press(self, pos: QPointF, button) -> None:
        if button != Qt.MouseButton.LeftButton:
            return
        self._press_seen = True
        self._mouse = pos
        if self._immersive:
            if self._immersive_back and hit(self._immerse_back.adjusted(-8, -8, 8, 8), pos):
                self.window.close_overlay()
            else:
                self._immersive_back = True
                self._hover = ""
            return
        if hit(QRectF(1810, 46, 54, 54), pos):
            self.window.close_overlay()
            return
        if hit(self._prev.adjusted(-5, -5, 5, 5), pos):
            self._step(-1)
            return
        if hit(self._next.adjusted(-5, -5, 5, 5), pos):
            self._step(1)
            return
        if hit(self._src.adjusted(-5, -5, 5, 5), pos):
            if self.items:
                paths.reveal(self.items[self.index].abs_path)
            return
        if hit(self._zoom_out.adjusted(-5, -5, 5, 5), pos):
            self._zoom_by(1 / 1.2)
            return
        if hit(self._zoom_in.adjusted(-5, -5, 5, 5), pos):
            self._zoom_by(1.2)
            return
        if hit(self._use_bg.adjusted(-5, -5, 5, 5), pos):
            if self.items:
                self.window.settings.set("title_bg", self.items[self.index].path)
                self._bg_toast_until = time.monotonic() + 2.4
            return
        if hit(self._immerse.adjusted(-5, -5, 5, 5), pos):
            self._immersive = True
            self._immersive_back = False
            self._hover = ""
            self.window.showFullScreen()
            return
        if self._content_rect().contains(pos) and self.zoom > 1.001:
            self._drag = True
            self._drag_start = pos
            self._pan_start = QPointF(self.pan)

    def mouse_release(self, pos: QPointF, button) -> None:
        self._drag = False

    def mouse_wheel(self, pos: QPointF, delta: int) -> None:
        if not delta or self._immersive:
            return
        old = self.zoom
        new = clamp(old * (1.16 if delta > 0 else 1 / 1.16), 1.0, 4.0)
        if new <= 1.001:
            self.zoom = 1.0
            self.pan = QPointF(0, 0)
            return
        base = self._fit_rect()
        if base.isNull():
            self.zoom = new
            return
        c = base.center() + self.pan
        rel = (pos - c) / old
        self.zoom = new
        self.pan = pos - base.center() - rel * new

    def key_press(self, key: int, mods) -> bool:
        if key == Qt.Key.Key_Escape:
            self.window.close_overlay()
            return True
        if key == Qt.Key.Key_I:
            self._immersive = not self._immersive
            self._immersive_back = False
            self._hover = ""
            if self._immersive:
                self.window.showFullScreen()
            return True
        if key == Qt.Key.Key_O:
            if self.items:
                paths.reveal(self.items[self.index].abs_path)
            return True
        if key in (Qt.Key.Key_Left, Qt.Key.Key_A):
            self._step(-1)
            return True
        if key in (Qt.Key.Key_Right, Qt.Key.Key_D):
            self._step(1)
            return True
        if key in (Qt.Key.Key_0, Qt.Key.Key_Home):
            self.zoom = 1.0
            self.pan = QPointF(0, 0)
            return True
        if key in (Qt.Key.Key_Plus, Qt.Key.Key_Equal):
            self._zoom_by(1.2)
            return True
        if key == Qt.Key.Key_Minus:
            self._zoom_by(1 / 1.2)
            return True
        if key == Qt.Key.Key_F:
            self.window.toggle_fullscreen()
            return True
        return False

    def _step(self, d: int) -> None:
        if not self.items:
            return
        if self._pix is not None and not self._pix.isNull():
            self._prev_pix = self._pix
            self._prev_rect = self._image_rect()
        else:
            self._prev_pix = None
            self._prev_rect = QRectF()
        self.index = (self.index + d) % len(self.items)
        self._step_dir = 1 if d > 0 else -1
        self._step_anim = 0.0
        self._load()

    def mouse_double_click(self, pos: QPointF) -> None:
        # The click that opened the viewer belongs to the gallery, so Qt
        # delivers only the DblClick half of that gesture to this new overlay.
        # Treat it as the opening click instead of a zoom request.
        if not self._press_seen:
            self._press_seen = True
            return
        if self.zoom > 1.001:
            self.zoom = 1.0
            self.pan = QPointF(0, 0)
        else:
            self.zoom = 2.0