from __future__ import annotations

import math
import time

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QFontMetricsF, QLinearGradient, QPainter, QPen, QRadialGradient

from .. import paths
from ..theme import CREAM, GOLD, GOLD_LIGHT, MUTED
from ..spectrum import Spectrum
from ..widgets import clamp, draw_icon, draw_icon_button, hit, lerp
from ..window import Screen


def fmt_time(ms: int) -> str:
    s = max(0, int(ms // 1000))
    return f"{s // 60:02d}:{s % 60:02d}"


class PlayerScreen(Screen):
    name = "player"

    def __init__(self, window) -> None:
        super().__init__(window)
        self.track = None
        self.t = 0.0
        self.ui_visible = True
        self.bar_anim = 1.0
        self.blur_anim = 0.0
        self.spec = Spectrum()
        self.last_mouse = time.monotonic()
        self.hover_ui = False
        self._buttons: list[tuple[str, QRectF]] = []
        self._vol_rect = QRectF()
        self._progress = QRectF()
        self._dragging = False
        self._drag_vol = False
        self._intro = 0.0
        self._active = False
        self._fade = 1.0
        self._last_pos = 0
        self._mouse = QPointF(-1, -1)
        self._back_top = QRectF(62, 70, 52, 52)
        self._src_top = QRectF(122, 166, 110, 32)
        self._title_top = QRectF(242, 166, 150, 32)

    # ------------------------------------------------------------------
    def enter(self, track=None, **kwargs) -> None:
        self._active = True
        self.t = 0.0
        self._fade = 1.0
        self._intro = 1.55
        self.ui_visible = True
        self.bar_anim = 1.0
        self.blur_anim = 0.0
        self.hover_ui = False
        self.last_mouse = time.monotonic()
        self.window.stage.set_cursor_hidden(False)
        self.window.audio.finished.connect(self._on_finished)
        if track is not None:
            self.set_track(track, autoplay=True)
        elif self.window.library.tracks:
            last = self.window.settings.get("last_track") or ""
            t = next((x for x in self.window.library.tracks if x.id == last), self.window.library.tracks[0])
            self.set_track(t, autoplay=True)

    def leave(self) -> None:
        self._active = False
        try:
            self.window.audio.finished.disconnect(self._on_finished)
        except Exception:
            pass
        # Music keeps playing while the user browses free play.
        self.window.stage.set_cursor_hidden(False)

    def set_track(self, track, autoplay: bool = True) -> None:
        changed = self.track is None or self.track.id != track.id
        self.track = track
        self.window.settings.set("last_track", track.id)
        if changed:
            self._intro = 1.35
            self.spec.bars[:] = 0
        if autoplay:
            audio = self.window.audio
            same_live = (audio.track is not None and audio.track.id == track.id
                         and audio.full_mode)
            if same_live:
                if not audio.playing:
                    audio.toggle()
            else:
                audio.play(track, 0, preview=False)
        else:
            self.window.audio.track = track

    # ------------------------------------------------------------------
    def tick(self, dt: float, now: float) -> None:
        self.t += dt
        self._fade = min(1.0, self._fade + dt / 0.62)
        if self._intro > 0:
            self._intro = max(0.0, self._intro - dt)
        if self.ui_visible and now - self.last_mouse > 2.6 and not self.hover_ui and not self._dragging:
            self.ui_visible = False
        if not self.ui_visible and not self._dragging:
            self.hover_ui = False
        self.bar_anim += ((1.0 if self.ui_visible else 0.0) - self.bar_anim) * (1 - math.exp(-dt / 0.19))
        want_blur = 1.0 if (self.hover_ui and self.window.settings.get("blur_cg", True)) else 0.0
        self.blur_anim += (want_blur - self.blur_anim) * (1 - math.exp(-dt / 0.19))
        active = self.window.audio.playing
        pos = self.window.audio.player.position() if self.track else 0
        self.spec.update(dt, pos, self.window.audio, active=active)
        if not self.ui_visible and now - self.last_mouse > 2.6:
            self.window.stage.set_cursor_hidden(True)
        else:
            self.window.stage.set_cursor_hidden(False)

    # ------------------------------------------------------------------
    def animating(self) -> bool:
        return self.bar_anim < 0.999 or self._intro > 0.01 or self.blur_anim < 0.999

    def _bar_rect(self) -> QRectF:
        y = 1080 - 132 - 26 + (1 - self.bar_anim) * 150
        return QRectF(60, y, 1800, 132)

    def _layout_bar(self) -> None:
        b = self._bar_rect()
        bx, by = b.x(), b.y()
        self._buttons = [
            ("prev", QRectF(bx + 1118, by + 61, 46, 46)),
            ("play", QRectF(bx + 1176, by + 54, 60, 60)),
            ("next", QRectF(bx + 1248, by + 61, 46, 46)),
            ("volume", QRectF(bx + 1316, by + 65, 40, 40)),
            ("full", QRectF(bx + 1676, by + 61, 46, 46)),
        ]
        self._vol_rect = QRectF(bx + 1366, by + 81, 92, 8)
        for i, label in enumerate(("脚灯", "光环", "关闭")):
            self._buttons.append((f"viz:{i}", QRectF(bx + 1476 + i * 60, by + 69, 54, 30)))
        self._progress = QRectF(bx + 34, by + 99, 1054, 6)

    # ------------------------------------------------------------------
    def paint(self, p: QPainter, now: float) -> None:
        t = self.track
        if t is None:
            p.fillRect(QRectF(0, 0, 1920, 1080), QColor(4, 3, 3))
            return
        a = self.window.assets
        theme = self.window.theme
        cg = a.act_cg(t, 3840, 2160)
        if not cg.isNull():
            p.drawPixmap(QRectF(0, 0, 1920, 1080), cg, QRectF(cg.rect()))
        else:
            p.fillRect(QRectF(0, 0, 1920, 1080), QColor(18, 12, 10))
        if self.blur_anim > 0.01:
            bl = a.blur_for(t)
            if not bl.isNull():
                p.setOpacity(self.blur_anim)
                p.drawPixmap(QRectF(0, 0, 1920, 1080), bl, QRectF(bl.rect()))
                p.setOpacity(1.0)
                p.fillRect(QRectF(0, 0, 1920, 1080), QColor(5, 4, 4, int(70 * self.blur_anim)))
        vig = a.ui("PZImgVignetting.png", 1920, 1080, "cover")
        if not vig.isNull():
            p.setOpacity(0.66)
            p.drawPixmap(QRectF(0, 0, 1920, 1080), vig, QRectF(vig.rect()))
            p.setOpacity(1.0)
        # cinematic top / bottom
        gt = QLinearGradient(0, 0, 0, 160)
        gt.setColorAt(0, QColor(5, 4, 4, 150))
        gt.setColorAt(1, QColor(5, 4, 4, 0))
        p.fillRect(QRectF(0, 0, 1920, 160), gt)
        gb = QLinearGradient(0, 900, 0, 1080)
        gb.setColorAt(0, QColor(5, 4, 4, 0))
        gb.setColorAt(1, QColor(5, 4, 4, 170))
        p.fillRect(QRectF(0, 900, 1920, 180), gb)

        viz = self.window.settings.get("viz", "footlights")
        if viz == "footlights":
            self.spec.draw_footlights(p, 1920, 1080, 0.95, lift=180 * self.bar_anim)
        elif viz == "halo":
            self.spec.draw_halo(p, 960, 500, 285, 0.72, QColor(226, 180, 112))

        # top-left act / back
        top_alpha = int(255 * clamp(self.bar_anim * 1.4))
        if top_alpha > 8:
            p.setPen(QPen(QColor(198, 156, 96, int(210 * self.bar_anim)), 2))
            p.drawLine(QPointF(58, 68), QPointF(58, 152))
            draw_icon_button(p, QRectF(67, 75, 42, 42), "down", self._back_top.contains(self._mouse), QColor(238, 224, 204, top_alpha))
            theme.text(p, 122, 92, t.act_label, theme.font(19, True), QColor(214, 174, 118, top_alpha))
            theme.text(p, 122, 126, t.act_zh, theme.font(32, True), QColor(240, 230, 214, top_alpha), shadow=True)
            theme.text(p, 124, 152, t.act_en, theme.font(14, False), QColor(200, 182, 158, int(top_alpha * 0.8)))
            hover_src = self._src_top.contains(self._mouse)
            p.setBrush(QColor(28, 22, 18, 220) if hover_src else QColor(16, 12, 10, 165))
            p.setPen(QPen(QColor(206, 168, 108, 210 if hover_src else 115), 1))
            p.drawRoundedRect(self._src_top, 5, 5)
            theme.text_center(p, self._src_top.center().x(), self._src_top.center().y() + 5,
                              "源文件", theme.font(13, False), QColor(238, 224, 204, top_alpha))
            is_title = str(self.window.settings.get("title_track") or "") == t.id
            hover_title = (not is_title and self.bar_anim > 0.35
                           and self._title_top.contains(self._mouse))
            p.setBrush(QColor(28, 22, 18, 220) if hover_title else QColor(16, 12, 10, 165))
            p.setPen(QPen(QColor(206, 168, 108, 210 if hover_title else (70 if is_title else 115)), 1))
            p.drawRoundedRect(self._title_top, 5, 5)
            theme.text_center(p, self._title_top.center().x(), self._title_top.center().y() + 5,
                              "已设为主界面" if is_title else "设为主界面音乐", theme.font(13, False),
                              QColor(238, 224, 204, int(top_alpha * (0.55 if is_title else 1.0))))

        # control bar
        self._layout_bar()
        if self.bar_anim > 0.01:
            self._paint_bar(p, t, now)

        # intro title card
        if self._intro > 0:
            k = self._intro
            alpha = 255 if k > 0.7 else int(255 * (k / 0.7))
            p.fillRect(QRectF(0, 0, 1920, 1080), QColor(3, 2, 2, int(alpha * 0.62)))
            if k > 0.25:
                theme.text_center(p, 960, 470, t.act_label, theme.font(20, True, spacing=4), QColor(216, 178, 122, alpha))
                p.setPen(QPen(QColor(204, 162, 102, int(alpha * 0.8)), 1.5))
                p.drawLine(QPointF(820, 498), QPointF(1100, 498))
                theme.text_center(p, 960, 556, t.title, theme.font(46, True), QColor(244, 234, 218, alpha), shadow=True)
                theme.text_center(p, 960, 600, t.artist, theme.font(18, False), QColor(210, 192, 170, int(alpha * 0.9)))
        if self._fade < 1.0:
            p.fillRect(QRectF(0, 0, 1920, 1080), QColor(0, 0, 0, int(255 * (1 - self._fade) ** 2)))

    def _hover_named(self, name: str) -> bool:
        for n, r in self._buttons:
            if n == name and hit(r.adjusted(-4, -4, 4, 4), self._mouse):
                return True
        return False

    # ------------------------------------------------------------------
    def _paint_bar(self, p: QPainter, t, now: float) -> None:
        b = self._bar_rect()
        theme = self.window.theme
        a = self.window.assets
        p.setOpacity(clamp(self.bar_anim * 1.2))
        p.setBrush(QColor(14, 11, 9, 218))
        p.setPen(QPen(QColor(168, 136, 90, 120), 1))
        p.drawRoundedRect(b, 5, 5)
        p.setPen(QPen(QColor(180, 150, 108, 70), 1))
        p.drawLine(QPointF(b.x() + 28, b.y() + 34), QPointF(b.right() - 28, b.y() + 34))
        # title / artist
        theme.text(p, b.x() + 34, b.y() + 30, t.title, theme.font(24, True), QColor(240, 230, 214, 255))
        sub = f"{t.act_label} · {t.act_zh}      {t.artist}      {t.quality}"
        theme.text(p, b.x() + 36, b.y() + 58, sub, theme.font(15, False), QColor(190, 172, 150, 235))
        # progress
        pos = self.window.audio.player.position() if self.window.audio.track and self.window.audio.track.id == t.id else 0
        dur = max(1, int(t.duration * 1000))
        frac = clamp(pos / dur)
        pr = self._progress
        p.setPen(QPen(QColor(84, 72, 62, 235), 3, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        p.drawLine(QPointF(pr.x(), pr.center().y()), QPointF(pr.right(), pr.center().y()))
        p.setPen(QPen(QColor(212, 172, 112, 255), 3, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        p.drawLine(QPointF(pr.x(), pr.center().y()), QPointF(pr.x() + pr.width() * frac, pr.center().y()))
        p.setBrush(QColor(240, 216, 180, 255))
        p.setPen(QPen(QColor(80, 62, 46, 180), 1))
        p.drawEllipse(QPointF(pr.x() + pr.width() * frac, pr.center().y()), 5, 5)
        theme.text(p, pr.x(), b.y() + 122, fmt_time(pos), theme.font(13, False), QColor(198, 182, 162, 230))
        total = fmt_time(dur)
        theme.text(p, pr.right() - theme.width(total, theme.font(13, False)), b.y() + 122, total,
                   theme.font(13, False), QColor(198, 182, 162, 230))
        # buttons
        playing = self.window.audio.playing
        for name, r in self._buttons:
            if name in ("prev", "play", "next", "volume", "full"):
                icon = {"prev": "prev", "play": "pause" if playing else "play", "next": "next",
                        "full": "fullscreen"}.get(name, "volume")
                if name == "volume":
                    icon = "mute" if self.window.audio.out.isMuted() or self.window.audio.volume() <= 0.001 else "volume"
                draw_icon_button(p, r, icon, self._hover_named(name), QColor(236, 222, 202, 255))
            elif name.startswith("viz:"):
                idx = int(name.split(":")[1])
                labels = ("脚灯", "光环", "关闭")
                mode = ("footlights", "halo", "off")[idx]
                active = self.window.settings.get("viz", "footlights") == mode
                rr = r
                p.setBrush(QColor(196, 154, 96, 220) if active else QColor(18, 14, 11, 170))
                p.setPen(QPen(QColor(206, 170, 112, 200 if active else 120), 1))
                p.drawRoundedRect(rr, 3, 3)
                theme.text_center(p, rr.center().x(), rr.y() + 19, labels[idx], theme.font(14, False),
                                  QColor(34, 24, 18, 255) if active else QColor(218, 202, 180, 235))
        # volume line
        vr = self._vol_rect
        vol = self.window.audio.volume()
        p.setPen(QPen(QColor(90, 76, 64, 230), 2, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        p.drawLine(QPointF(vr.x(), vr.center().y()), QPointF(vr.right(), vr.center().y()))
        p.setPen(QPen(QColor(206, 168, 108, 240), 2, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        p.drawLine(QPointF(vr.x(), vr.center().y()), QPointF(vr.x() + vr.width() * vol, vr.center().y()))
        p.setBrush(QColor(236, 214, 182, 255))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawEllipse(QPointF(vr.x() + vr.width() * vol, vr.center().y()), 4, 4)
        p.setOpacity(1.0)

    # ------------------------------------------------------------------
    def mouse_move(self, pos: QPointF) -> None:
        self._mouse = pos
        self.last_mouse = time.monotonic()
        self.ui_visible = True
        self.window.stage.set_cursor_hidden(False)
        self._layout_bar()
        self.hover_ui = False
        for name, r in self._buttons:
            if hit(r.adjusted(-6, -6, 6, 6), pos):
                self.hover_ui = True
                break
        if hit(self._progress.adjusted(0, -14, 0, 14), pos):
            self.hover_ui = True
        if hit(self._vol_rect.adjusted(0, -14, 0, 14), pos):
            self.hover_ui = True
        if hit(QRectF(56, 40, 620, 150), pos):
            self.hover_ui = True
        if self._back_top.contains(pos) or self._src_top.contains(pos):
            self.hover_ui = True
        if (self.bar_anim > 0.35 and self._title_top.contains(pos)
                and self.track is not None
                and str(self.window.settings.get("title_track") or "") != self.track.id):
            self.hover_ui = True
        # any movement over the lower control deck blurs the CG, not only a
        # button itself
        if pos.y() >= 884:
            self.hover_ui = True
        if self._dragging:
            self._seek_from_pos(pos)
        if self._drag_vol:
            self._set_volume_from_pos(pos)

    def mouse_press(self, pos: QPointF, button) -> None:
        if button != Qt.MouseButton.LeftButton:
            return
        self.last_mouse = time.monotonic()
        self._mouse = pos
        self.ui_visible = True
        self._layout_bar()
        if self._back_top.contains(pos):
            self.window.back_to_freeplay(track=self.track, suppress_preview=True)
            return
        if self._src_top.contains(pos):
            paths.reveal(self.track.abs_path)
            return
        if self.bar_anim > 0.35 and self._title_top.contains(pos):
            if str(self.window.settings.get("title_track") or "") != self.track.id:
                self.window.settings.set("title_track", self.track.id)
            return
        for name, r in self._buttons:
            if hit(r.adjusted(-5, -5, 5, 5), pos):
                if name == "prev":
                    self._step(-1)
                elif name == "next":
                    self._step(1)
                elif name == "play":
                    self.window.audio.toggle()
                elif name == "volume":
                    self.window.audio.set_muted(not self.window.audio.out.isMuted())
                    self.window.settings.set("muted", self.window.audio.out.isMuted())
                elif name == "full":
                    self.window.toggle_fullscreen()
                elif name.startswith("viz:"):
                    idx = int(name.split(":")[1])
                    self.window.settings.set("viz", ("footlights", "halo", "off")[idx])
                return
        if hit(self._progress.adjusted(0, -14, 0, 14), pos):
            self._dragging = True
            self._seek_from_pos(pos)
            return
        if hit(self._vol_rect.adjusted(0, -14, 0, 14), pos):
            self._drag_vol = True
            self._set_volume_from_pos(pos)
            return
        if hit(self._bar_rect(), pos) or hit(QRectF(56, 40, 600, 130), pos):
            return
        # click on the CG toggles playback
        self.window.audio.toggle()

    def mouse_release(self, pos: QPointF, button) -> None:
        self._dragging = False
        self._drag_vol = False

    def mouse_wheel(self, pos: QPointF, delta: int) -> None:
        v = self.window.audio.volume() + (0.03 if delta > 0 else -0.03)
        self.window.audio.set_volume(v)
        self.window.settings.set("volume", clamp(v))
        self.ui_visible = True
        self.last_mouse = time.monotonic()

    def key_press(self, key: int, mods) -> bool:
        ctrl = bool(mods & Qt.KeyboardModifier.ControlModifier)
        if key == Qt.Key.Key_Space:
            self.window.audio.toggle()
            self.ui_visible = True
            self.last_mouse = time.monotonic()
            return True
        if key in (Qt.Key.Key_Left, Qt.Key.Key_Right):
            if ctrl:
                self._step(-1 if key == Qt.Key.Key_Left else 1)
            else:
                d = -5000 if key == Qt.Key.Key_Left else 5000
                self.window.audio.seek(self.window.audio.player.position() + d)
            return True
        if key in (Qt.Key.Key_Up, Qt.Key.Key_Down):
            d = 0.04 if key == Qt.Key.Key_Up else -0.04
            v = clamp(self.window.audio.volume() + d)
            self.window.audio.set_volume(v)
            self.window.settings.set("volume", v)
            return True
        if key in (Qt.Key.Key_Escape,):
            self.window.back_to_freeplay(track=self.track, suppress_preview=True)
            return True
        if key == Qt.Key.Key_F:
            self.window.toggle_fullscreen()
            return True
        if key == Qt.Key.Key_M:
            self.window.audio.set_muted(not self.window.audio.out.isMuted())
            return True
        if key == Qt.Key.Key_V:
            modes = ("footlights", "halo", "off")
            cur = self.window.settings.get("viz", "footlights")
            self.window.settings.set("viz", modes[(modes.index(cur) + 1) % 3])
            return True
        return False

    # ------------------------------------------------------------------
    def _step(self, delta: int) -> None:
        nxt = self.window.library.next_track(self.track, delta)
        if nxt is not None:
            self.set_track(nxt, autoplay=True)

    def _on_finished(self) -> None:
        if not self._active:
            return
        nxt = self.window.library.next_track(self.track, 1)
        if nxt is not None:
            self.set_track(nxt, autoplay=True)

    def _seek_from_pos(self, pos: QPointF) -> None:
        if self.track is None:
            return
        f = clamp((pos.x() - self._progress.x()) / self._progress.width())
        self.window.audio.seek(int(f * self.track.duration * 1000))

    def _set_volume_from_pos(self, pos: QPointF) -> None:
        f = clamp((pos.x() - self._vol_rect.x()) / self._vol_rect.width())
        self.window.audio.set_volume(f)
        self.window.audio.set_muted(False)
        self.window.settings.set("volume", f)
        self.window.settings.set("muted", False)
