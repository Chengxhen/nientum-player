from __future__ import annotations

import time

from PySide6.QtCore import QByteArray, QPointF, QRectF, QSize, Qt, QTimer
from PySide6.QtGui import QColor, QIcon, QKeyEvent, QLinearGradient, QPainter, QPen, QPixmap
from PySide6.QtWidgets import QApplication, QMainWindow, QWidget

from . import applog, paths
from .assets import Assets
from .audio import AudioEngine
from .curtain import Curtain
from .models import Library
from .settings import Settings
from .theme import Theme


class StageWidget(QWidget):
    def __init__(self, window: "NientumWindow") -> None:
        super().__init__(window)
        self.window = window
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.FocusPolicy.StrongFocus)
        self.setAttribute(Qt.WidgetAttribute.WA_OpaquePaintEvent, True)
        self._last = time.monotonic()
        self._cursor_hidden = False
        self._render_pm = None
        self._render_key = None
        self._curtain_last = time.monotonic()
        self._curtain_timer = QTimer(self)
        self._curtain_timer.setInterval(4)
        self._curtain_timer.setTimerType(Qt.TimerType.PreciseTimer)
        self._curtain_timer.timeout.connect(self._curtain_tick)
        self.timer = QTimer(self)
        self.timer.setInterval(8)
        self.timer.setTimerType(Qt.TimerType.PreciseTimer)
        self.timer.timeout.connect(self._tick)
        self.timer.start()

    def set_fps(self, fps) -> None:
        try:
            fps = int(fps)
        except Exception:
            fps = 60
        if fps <= 0:
            # 0 is treated as the high-refresh 250 Hz path.
            self.timer.setInterval(4)
        else:
            self.timer.setInterval(max(2, int(round(1000.0 / max(1, fps)))))

    def viewport_rect(self) -> QRectF:
        w, h = self.width(), self.height()
        if w <= 0 or h <= 0:
            return QRectF(0, 0, 1920, 1080)
        vw = min(w, h * 16 / 9)
        vh = vw * 9 / 16
        return QRectF((w - vw) / 2, (h - vh) / 2, vw, vh)

    def _scale(self) -> float:
        return max(0.0001, self.viewport_rect().width() / 1920.0)

    def to_logical(self, pos: QPointF) -> QPointF | None:
        vp = self.viewport_rect()
        if not vp.contains(pos):
            return None
        s = self._scale()
        return QPointF((pos.x() - vp.x()) / s, (pos.y() - vp.y()) / s)

    def set_cursor_hidden(self, hidden: bool) -> None:
        if hidden == self._cursor_hidden:
            return
        self._cursor_hidden = hidden
        self.setCursor(Qt.CursorShape.BlankCursor if hidden else Qt.CursorShape.ArrowCursor)

    def _tick(self) -> None:
        now = time.monotonic()
        dt = min(0.05, max(0.0, now - self._last))
        self._last = now
        win = self.window
        if win.curtain.active:
            if not self._curtain_timer.isActive():
                self._curtain_last = now
                self._curtain_timer.start()
        elif self._curtain_timer.isActive():
            self._curtain_timer.stop()
        if win.slide is not None:
            win.slide["t"] = min(1.0, win.slide["t"] + dt / win.slide["dur"])
            if win.slide["t"] >= 1.0:
                win.slide = None
        if win.overlay is not None:
            win.overlay.tick(dt, now)
        if win.screen is not None:
            win.screen.tick(dt, now)
        self.update()

    def _curtain_tick(self) -> None:
        now = time.monotonic()
        dt = min(0.05, max(0.0, now - self._curtain_last))
        self._curtain_last = now
        if self.window.curtain.active:
            self.window.curtain.update(dt)
            self.update()
        else:
            self._curtain_timer.stop()

    def paintEvent(self, event) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
        p.setRenderHint(QPainter.RenderHint.TextAntialiasing, True)
        p.fillRect(self.rect(), QColor(13, 9, 7))
        vp = self.viewport_rect()
        # A theatre matte instead of a hard black band around the 16:9
        # stage.  In normal windowed mode the window is kept 16:9, so these
        # only show in fullscreen on a non-16:9 monitor.
        has_mat = (vp.top() > 0.5 or vp.bottom() < self.height() - 0.5
                   or vp.left() > 0.5 or vp.right() < self.width() - 0.5)
        if has_mat:
            if vp.top() > 0.5:
                g = QLinearGradient(0, 0, 0, vp.top())
                g.setColorAt(0, QColor(5, 4, 3))
                g.setColorAt(1, QColor(27, 16, 12, 248))
                p.fillRect(QRectF(0, 0, self.width(), vp.top()), g)
            if vp.bottom() < self.height() - 0.5:
                g = QLinearGradient(0, vp.bottom(), 0, self.height())
                g.setColorAt(0, QColor(27, 16, 12, 248))
                g.setColorAt(1, QColor(5, 4, 3))
                p.fillRect(QRectF(0, vp.bottom(), self.width(), self.height() - vp.bottom()), g)
            if vp.left() > 0.5:
                g = QLinearGradient(0, 0, vp.left(), 0)
                g.setColorAt(0, QColor(5, 4, 3))
                g.setColorAt(1, QColor(27, 16, 12, 248))
                p.fillRect(QRectF(0, 0, vp.left(), self.height()), g)
            if vp.right() < self.width() - 0.5:
                g = QLinearGradient(vp.right(), 0, self.width(), 0)
                g.setColorAt(0, QColor(27, 16, 12, 248))
                g.setColorAt(1, QColor(5, 4, 3))
                p.fillRect(QRectF(vp.right(), 0, self.width() - vp.right(), self.height()), g)
            p.setPen(QPen(QColor(194, 154, 102, 82), 1))
            if vp.top() > 0.5:
                p.drawLine(vp.topLeft(), vp.topRight())
            if vp.bottom() < self.height() - 0.5:
                p.drawLine(vp.bottomLeft(), vp.bottomRight())
            if vp.left() > 0.5:
                p.drawLine(vp.topLeft(), vp.bottomLeft())
            if vp.right() < self.width() - 0.5:
                p.drawLine(vp.topRight(), vp.bottomRight())
        now = time.monotonic()
        try:
            res = int(self.window.settings.get("resolution", 1080))
        except Exception:
            res = 1080
        factor = max(0.5, min(2.0, res / 1080.0))
        animating = self.window.slide is not None or self.window.curtain.active
        if not animating and self.window.screen is not None and hasattr(self.window.screen, "animating"):
            try:
                animating = bool(self.window.screen.animating())
            except Exception:
                animating = False
        if animating and factor > 1.0:
            factor = 1.0
        if abs(factor - 1.0) < 0.002:
            s = vp.width() / 1920.0
            p.save()
            p.translate(vp.x(), vp.y())
            p.scale(s, s)
            p.setClipRect(QRectF(0, 0, 1920, 1080))
            self._paint_scene(p, now)
            p.restore()
        else:
            sw, sh = max(1, int(round(1920 * factor))), max(1, int(round(1080 * factor)))
            key = (sw, sh)
            if self._render_key != key:
                self._render_pm = QPixmap(sw, sh)
                self._render_key = key
            self._render_pm.fill(QColor(0, 0, 0, 0))
            rp = QPainter(self._render_pm)
            rp.setRenderHint(QPainter.RenderHint.Antialiasing, True)
            rp.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
            rp.setRenderHint(QPainter.RenderHint.TextAntialiasing, True)
            rp.scale(factor, factor)
            rp.setClipRect(QRectF(0, 0, 1920, 1080))
            self._paint_scene(rp, now)
            rp.end()
            p.drawPixmap(vp, self._render_pm, QRectF(self._render_pm.rect()))
        p.end()

    def _paint_scene(self, p: QPainter, now: float) -> None:
        if self.window.slide is not None:
            self._paint_slide(p, now)
        else:
            if self.window.screen is not None:
                self.window.screen.paint(p, now)
            if self.window.overlay is not None:
                self.window.overlay.paint(p, now)
        if self.window.curtain.active:
            self.window.curtain.paint(p, 1920, 1080)

    def _paint_slide(self, p: QPainter, now: float) -> None:
        s = self.window.slide
        if not s:
            return
        k = s["t"]
        e = k * k * (3 - 2 * k)
        prev = s.get("prev")
        if s["phase"] == "up":
            # old screen stays put; the new one rises from below
            if prev is not None and not prev.isNull():
                p.drawPixmap(0, 0, 1920, 1080, prev)
            off = (1.0 - e) * 1080.0
            p.save()
            p.translate(0, off)
            if self.window.screen is not None:
                self.window.screen.paint(p, now)
            p.restore()
        else:
            # new screen is already visible behind; the old one drops away
            if self.window.screen is not None:
                self.window.screen.paint(p, now)
            off = e * 1080.0
            p.save()
            p.translate(0, off)
            if prev is not None and not prev.isNull():
                p.drawPixmap(0, 0, 1920, 1080, prev)
            p.restore()
        if 1.0 < off < 1079.0:
            sh = QLinearGradient(0, off - 34, 0, off)
            sh.setColorAt(0, QColor(0, 0, 0, 0))
            sh.setColorAt(1, QColor(0, 0, 0, 155))
            p.fillRect(QRectF(0, off - 34, 1920, 34), sh)
            p.setPen(QPen(QColor(202, 162, 104, 130), 1))
            p.drawLine(QPointF(0, off), QPointF(1920, off))

    # ------------------------------------------------------------------
    def mouseMoveEvent(self, event) -> None:
        self.set_cursor_hidden(False)
        pos = self.to_logical(event.position())
        if pos is None or self.window.curtain.active or self.window.slide is not None:
            return
        if self.window.overlay is not None:
            self.window.overlay.mouse_move(pos)
        elif self.window.screen is not None:
            self.window.screen.mouse_move(pos)

    def mousePressEvent(self, event) -> None:
        self.setFocus()
        pos = self.to_logical(event.position())
        if pos is None or self.window.curtain.active or self.window.slide is not None:
            return
        if self.window.overlay is not None:
            self.window.overlay.mouse_press(pos, event.button())
        elif self.window.screen is not None:
            self.window.screen.mouse_press(pos, event.button())

    def mouseReleaseEvent(self, event) -> None:
        pos = self.to_logical(event.position())
        if pos is None or self.window.curtain.active or self.window.slide is not None:
            return
        if self.window.overlay is not None:
            self.window.overlay.mouse_release(pos, event.button())
        elif self.window.screen is not None:
            self.window.screen.mouse_release(pos, event.button())

    def mouseDoubleClickEvent(self, event) -> None:
        pos = self.to_logical(event.position())
        if pos is None or self.window.curtain.active or self.window.slide is not None:
            return
        if self.window.overlay is not None and hasattr(self.window.overlay, "mouse_double_click"):
            self.window.overlay.mouse_double_click(pos)
        elif self.window.screen is not None:
            self.window.screen.mouse_double_click(pos)

    def wheelEvent(self, event) -> None:
        pos = self.to_logical(event.position())
        if pos is None or self.window.curtain.active or self.window.slide is not None:
            return
        delta = event.angleDelta().y()
        if self.window.overlay is not None:
            self.window.overlay.mouse_wheel(pos, delta)
        elif self.window.screen is not None:
            self.window.screen.mouse_wheel(pos, delta)

    def keyPressEvent(self, event: QKeyEvent) -> None:
        if event.key() == Qt.Key.Key_F11:
            self.window.toggle_fullscreen()
            return
        if event.key() == Qt.Key.Key_Escape and self.isFullScreen():
            self.window.showNormal()
            return
        if self.window.curtain.active or self.window.slide is not None:
            return
        if self.window.overlay is not None:
            if self.window.overlay.key_press(event.key(), event.modifiers()):
                return
        if self.window.screen is not None:
            self.window.screen.key_press(event.key(), event.modifiers())


class Screen:
    name = ""

    def __init__(self, window: "NientumWindow") -> None:
        self.window = window

    @property
    def app(self) -> "NientumWindow":
        return self.window

    def enter(self, **kwargs) -> None:
        pass

    def leave(self) -> None:
        pass

    def tick(self, dt: float, now: float) -> None:
        pass

    def paint(self, p: QPainter, now: float) -> None:
        pass

    def animating(self) -> bool:
        return False

    def mouse_move(self, pos: QPointF) -> None:
        pass

    def mouse_press(self, pos: QPointF, button) -> None:
        pass

    def mouse_release(self, pos: QPointF, button) -> None:
        pass

    def mouse_wheel(self, pos: QPointF, delta: int) -> None:
        pass

    def key_press(self, key: int, mods) -> bool:
        return False


class NientumWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Nientum - Op.ZERO · 音乐与 CG 鉴赏")
        self.setMinimumSize(960, 540)
        self.resize(1600, 900)
        self.assets = Assets()
        self.theme = Theme()
        self.library = Library()
        self.settings = Settings()
        self.audio = AudioEngine()
        self.audio.set_volume(float(self.settings.get("volume")))
        self.audio.set_muted(bool(self.settings.get("muted")))
        dev = int(self.settings.get("device") or -1)
        if dev >= 0:
            self.audio.set_output_device(dev)
        self.curtain = Curtain(self.assets)
        self.slide = None
        self._aspect_guard = False
        self.stage = StageWidget(self)
        self.setCentralWidget(self.stage)
        try:
            start_fps = int(self.settings.get("fps", 60))
        except Exception:
            start_fps = 60
        self.stage.set_fps(start_fps)
        self.screen = None
        self.current_name = ""
        self.overlay = None
        self._make_icon()
        # screens
        from .screens.freeplay import FreePlayScreen
        from .screens.gallery import GalleryScreen
        from .screens.player import PlayerScreen
        from .screens.title import TitleScreen
        self.screens = {
            "title": TitleScreen(self),
            "freeplay": FreePlayScreen(self),
            "player": PlayerScreen(self),
            "gallery": GalleryScreen(self),
        }
        geom = self.settings.get("geometry")
        if geom:
            try:
                self.restoreGeometry(QByteArray.fromBase64(geom.encode("ascii")))
            except Exception:
                self._center()
        else:
            self._center()
        self.show_screen("title", transition=None)
        self.stage.setFocus()

    def showEvent(self, event) -> None:
        super().showEvent(event)
        if getattr(self, "_dwm_done", False):
            return
        self._dwm_done = True
        try:
            import ctypes
            hwnd = int(self.winId())
            dwm = ctypes.windll.dwmapi
            dark = ctypes.c_int(1)
            dwm.DwmSetWindowAttribute(hwnd, 20, ctypes.byref(dark), ctypes.sizeof(dark))
            caption = ctypes.c_int(0x0008090C)
            dwm.DwmSetWindowAttribute(hwnd, 35, ctypes.byref(caption), ctypes.sizeof(caption))
            text = ctypes.c_int(0x00DCE6F0)
            dwm.DwmSetWindowAttribute(hwnd, 36, ctypes.byref(text), ctypes.sizeof(text))
            border = ctypes.c_int(0x00608CA8)
            dwm.DwmSetWindowAttribute(hwnd, 34, ctypes.byref(border), ctypes.sizeof(border))
        except Exception:
            pass

    def _center(self) -> None:
        screen = QApplication.primaryScreen()
        if not screen:
            return
        geo = screen.availableGeometry()
        w = min(1600, geo.width() - 80)
        h = int(w * 9 / 16)
        if h > geo.height() - 80:
            h = geo.height() - 80
            w = int(h * 16 / 9)
        self.resize(w, h)
        self.move(geo.center().x() - w // 2, geo.center().y() - h // 2)

    def _make_icon(self) -> None:
        pix = QPixmap(256, 256)
        pix.fill(QColor(12, 9, 8))
        p = QPainter(pix)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
        logo = self.assets.ui("PZImgNIENTUM_logo.png", 230, 144, "contain")
        if not logo.isNull():
            p.drawPixmap(13, 56, logo)
        p.end()
        self.setWindowIcon(QIcon(pix))

    # ------------------------------------------------------------------
    def show_screen(self, name: str, transition: str | None = "curtain", **kwargs) -> None:
        if name == self.current_name and not kwargs:
            return
        applog.log(f"screen request: {name} transition={transition}")
        if transition == "curtain" and self.screen is not None:
            self.overlay = None
            self.curtain.start(lambda: self._set_screen(name, **kwargs))
        else:
            self._set_screen(name, **kwargs)

    def _set_screen(self, name: str, **kwargs) -> None:
        if self.screen is not None:
            self.screen.leave()
        self.screen = self.screens[name]
        self.current_name = name
        self.overlay = None
        self.screen.enter(**kwargs)
        applog.log(f"screen entered: {name}")
        self.stage.setFocus()

    def _render_screen_pixmap(self) -> QPixmap:
        pm = QPixmap(1920, 1080)
        pm.fill(QColor(0, 0, 0))
        p = QPainter(pm)
        p.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
        p.setRenderHint(QPainter.RenderHint.TextAntialiasing, True)
        now = time.monotonic()
        try:
            if self.screen is not None:
                self.screen.paint(p, now)
            if self.overlay is not None:
                self.overlay.paint(p, now)
        finally:
            p.end()
        return pm

    def slide_to(self, name: str, phase: str, **kwargs) -> None:
        """Slide a screen in from below (phase='up') or drop the old one
        away downwards (phase='down').  The outgoing screen is frozen into a
        snapshot so the whole panel moves as one piece."""
        prev = self._render_screen_pixmap()
        self._set_screen(name, **kwargs)
        self.slide = {"prev": prev, "phase": phase, "t": 0.0, "dur": 0.62}

    def open_player(self, track) -> None:
        self.slide_to("player", "up", track=track)

    def back_to_freeplay(self, **kwargs) -> None:
        if self.current_name == "player":
            self.slide_to("freeplay", "down", **kwargs)
        else:
            self.show_screen("freeplay", transition="curtain", **kwargs)

    def open_gallery(self, category: str | None = None, index: int = 0) -> None:
        self.show_screen("gallery", transition="curtain", category=category, index=index)

    def open_viewer(self, category: str, index: int = 0, items=None, label: str | None = None) -> None:
        from .screens.viewer import GalleryViewer
        self.overlay = GalleryViewer(self, category, index, items=items, label=label)

    def open_settings(self) -> None:
        from .screens.settings import SettingsOverlay
        self.overlay = SettingsOverlay(self)

    def close_overlay(self) -> None:
        if self.overlay is not None and hasattr(self.overlay, "commit"):
            try:
                self.overlay.commit()
            except Exception:
                pass
        self.overlay = None
        if self.screen is not None and hasattr(self.screen, "on_overlay_closed"):
            try:
                self.screen.on_overlay_closed()
            except Exception:
                pass

    def toggle_fullscreen(self) -> None:
        if self.isFullScreen():
            self.showNormal()
        else:
            self.showFullScreen()

    def quit_app(self) -> None:
        self.audio.stop()
        self.close()

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        if getattr(self, "_aspect_guard", False):
            return
        if self.isMaximized() or self.isFullScreen():
            return
        stage = getattr(self, "stage", None)
        if stage is None:
            return
        w, h = stage.width(), stage.height()
        if w <= 0 or h <= 0:
            return
        target = int(round(w * 9 / 16))
        if abs(h - target) <= 1:
            return
        self._aspect_guard = True
        try:
            self.resize(self.width(), max(self.minimumHeight(), self.height() + target - h))
        finally:
            self._aspect_guard = False

    def closeEvent(self, event) -> None:
        if self.overlay is not None and hasattr(self.overlay, "commit"):
            try:
                self.overlay.commit()
            except Exception:
                pass
        try:
            self.settings.set("geometry", bytes(self.saveGeometry().toBase64()).decode("ascii"))
        except Exception:
            pass
        try:
            fp = self.screens.get("freeplay")
            if fp is not None:
                self.settings.set("lp_angle", round(fp.spin, 3))
        except Exception:
            pass
        self.audio.stop()
        event.accept()
