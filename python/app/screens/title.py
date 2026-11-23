from __future__ import annotations

import math
import random

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QLinearGradient, QMovie, QPainter, QPen, QPixmap, QPolygonF, QRadialGradient

from .. import applog, paths
from ..theme import CREAM, GOLD, GOLD_DEEP, GOLD_LIGHT, MUTED, RED
from ..widgets import clamp, draw_icon_button, hit, lerp
from ..window import Screen


class TitleScreen(Screen):
    name = "title"

    def __init__(self, window) -> None:
        super().__init__(window)
        self.t = 0.0
        self.selected = 0
        self.items = [
            ("自由选曲", "FREE PLAY", "freeplay"),
            ("CG 鉴赏", "COLLECTION", "gallery"),
            ("设置", "SETTINGS", "settings"),
            ("退出", "QUIT", "quit"),
        ]
        self._rects: list[QRectF] = []
        rnd = random.Random(21)
        self.petals = [
            [rnd.uniform(0, 1920), rnd.uniform(-100, 1080), rnd.uniform(8, 24),
             rnd.uniform(1.2, 3.4), rnd.uniform(0, 6.28), rnd.uniform(18, 60)]
            for _ in range(26)
        ]
        self._tachie = None
        self._shadow = None
        self._movies = []
        self._movies_ready = False
        self._fullscreen_rect = QRectF(1818, 58, 44, 44)
        self._hover_full = False
        self._menu_focus = 0.0
        self._bg_cache = None
        self._bg_key = None

    def enter(self, **kwargs) -> None:
        self.t = 0.0
        self._hover_full = False
        self._menu_focus = float(self.selected)
        # Keep the concert going when coming back from the gallery / player.
        # On a fresh start, use the chosen title song, or the game's Liora's
        # Song as the default.
        audio = self.window.audio
        if not (audio.full_mode and audio.track is not None):
            tid = str(self.window.settings.get("title_track") or "")
            track = next((t for t in self.window.library.tracks if t.id == tid), None)
            if track is None:
                track = next((t for t in self.window.library.tracks if "Liora's Song Unwithering" in t.title), None)
                if track is None:
                    track = next((t for t in self.window.library.tracks if "Liora's Song" in t.title), None)
            if track is None and self.window.library.tracks:
                track = self.window.library.tracks[0]
            if track is not None:
                audio.play(track, 0, preview=False)
        self._ensure_movies()
        self.window.stage.set_cursor_hidden(False)

    def leave(self) -> None:
        self.window.audio.stop_title_bgm()
        for movie in self._movies:
            movie.stop()

    def _ensure_movies(self) -> None:
        if self._movies_ready:
            for movie in self._movies:
                movie.start()
            return
        self._movies_ready = True
        for name in ("liora", "aleph"):
            p = paths.data("anim", f"{name}_idle.webp")
            movie = QMovie(str(p))
            movie.setCacheMode(QMovie.CacheMode.CacheNone)
            if movie.isValid():
                movie.start()
                self._movies.append(movie)
        if len(self._movies) != 2:
            applog.log("title spine unavailable, using static tachie")
            self._movies = []
        else:
            applog.log("title spine animation loaded")

    def _layout(self) -> None:
        self._rects = [QRectF(112, 662 + i * 64, 380, 56) for i in range(len(self.items))]

    def tick(self, dt: float, now: float) -> None:
        self.t += dt
        for p in self.petals:
            p[0] += math.sin(self.t * 0.4 + p[4]) * 10 * dt
            p[1] += p[2] * dt
            if p[1] > 1120:
                p[1] = -30
                p[0] = random.uniform(0, 1920)
        if self._rects == []:
            self._layout()
        self._menu_focus += (self.selected - self._menu_focus) * (1 - math.exp(-dt / 0.12))
        if abs(self.selected - self._menu_focus) < 0.002:
            self._menu_focus = float(self.selected)

    # ------------------------------------------------------------------
    def animating(self) -> bool:
        return abs(self._menu_focus - self.selected) > 0.002

    def _build_background(self, custom: str) -> QPixmap:
        pm = QPixmap(1920, 1080)
        pm.fill(QColor(12, 10, 9))
        q = QPainter(pm)
        q.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
        q.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        a = self.window.assets
        drawn = False
        if custom:
            pix = a.full(custom, 1920)
            if not pix.isNull():
                q.drawPixmap(QRectF(0, 0, 1920, 1080), pix, QRectF(pix.rect()))
                # Keep the logo/menu side readable while leaving the right
                # side of the chosen CG at its normal brightness.
                shade = QLinearGradient(QPointF(0, 0), QPointF(1400, 0))
                shade.setColorAt(0.0, QColor(4, 3, 3, 195))
                shade.setColorAt(0.42, QColor(4, 3, 3, 120))
                shade.setColorAt(0.78, QColor(4, 3, 3, 28))
                shade.setColorAt(1.0, QColor(4, 3, 3, 0))
                q.fillRect(QRectF(0, 0, 1920, 1080), shade)
                light = a.ui("PZImgTitleBackgroundLight.png", 1920, 1080, "cover")
                if not light.isNull():
                    q.setOpacity(0.14)
                    q.drawPixmap(0, 0, 1920, 1080, light)
                    q.setOpacity(1.0)
                drawn = True
        if not drawn:
            bg = a.ui("PZImgTitleBackground.png", 1920, 1080, "cover")
            if not bg.isNull():
                q.drawPixmap(0, 0, 1920, 1080, bg)
            light = a.ui("PZImgTitleBackgroundLight.png", 1920, 1080, "cover")
            if not light.isNull():
                q.setOpacity(0.50)
                q.drawPixmap(0, 0, 1920, 1080, light)
                q.setOpacity(1.0)
        # spotlight behind the pair
        q.setCompositionMode(QPainter.CompositionMode.CompositionMode_Plus)
        rg = QRadialGradient(QPointF(980, 500), 620)
        rg.setColorAt(0, QColor(255, 214, 156, 36))
        rg.setColorAt(0.55, QColor(255, 190, 120, 12))
        rg.setColorAt(1, QColor(255, 190, 120, 0))
        q.setBrush(rg)
        q.setPen(Qt.PenStyle.NoPen)
        q.drawEllipse(QPointF(980, 500), 620, 620)
        q.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)
        q.end()
        return pm

    def paint(self, p: QPainter, now: float) -> None:
        self._layout()
        custom = self.window.settings.get("title_bg") or ""
        if self._bg_cache is None or self._bg_key != custom:
            self._bg_cache = self._build_background(custom)
            self._bg_key = custom
        p.drawPixmap(0, 0, 1920, 1080, self._bg_cache)
        a = self.window.assets

        # animated title characters only belong to the default title art
        if not custom:
            self._paint_characters(p)

        # petals / dust
        p.setCompositionMode(QPainter.CompositionMode.CompositionMode_Plus)
        for x, y, spd, size, ph, alpha in self.petals:
            c = QColor(247, 214, 164, int(alpha * 0.7))
            p.setBrush(c)
            p.setPen(Qt.PenStyle.NoPen)
            p.save()
            p.translate(x, y)
            p.rotate(self.t * 25 + ph * 57)
            p.drawEllipse(QPointF(0, 0), size, size * 0.45)
            p.restore()
        p.setCompositionMode(QPainter.CompositionMode.CompositionMode_SourceOver)

        # logo
        logo = a.logo(540)
        if not logo.isNull():
            p.drawPixmap(78, 82, logo)
            # small rule + subtitle under logo
            p.setPen(QPen(QColor(198, 158, 102, 170), 1.2))
            p.drawLine(QPointF(84, 440), QPointF(470, 440))
            self.window.theme.text(p, 88, 472, "MUSIC  &  MEMORY  OF  THE  STAGE",
                                   self.window.theme.font(14, False, spacing=2.4),
                                   QColor(214, 190, 162, 220))

        # Game title selection: no rectangular panel; the game's own
        # glow and ornamental top/bottom frame are centered on the Chinese
        # option, with no English label on the right.
        theme = self.window.theme
        focus = self._menu_focus
        row_y = 662.0 + focus * 64.0
        sel = max(0, min(len(self.items) - 1, self.selected))
        zh_font = theme.font(30, True, spacing=1.2)
        zh_w = theme.metrics(zh_font).horizontalAdvance(self.items[sel][0])
        text_cx = 112.0 + 190.0
        frame_w = max(240.0, zh_w + 88.0)
        frame_x = text_cx - frame_w / 2
        glow = a.ui("PZImgActive_glow.png", 340, 220, "contain")
        if not glow.isNull():
            p.save()
            p.setCompositionMode(QPainter.CompositionMode.CompositionMode_Plus)
            p.setOpacity(0.40)
            p.drawPixmap(QRectF(text_cx - 170, row_y - 90, 340, 220), glow, QRectF(glow.rect()))
            p.restore()
        frame = a.ui("PZImgframe.png", int(frame_w), 20, "contain")
        if not frame.isNull():
            fm = theme.metrics(zh_font)
            baseline = 662.0 + sel * 64.0 + 32.0
            text_top = baseline - fm.ascent()
            text_bottom = baseline + fm.descent()
            gap = 3.0
            fh = 20.0
            p.save()
            p.setOpacity(0.92)
            top = QRectF(frame_x, text_top - gap - fh, frame_w, fh)
            p.drawPixmap(top, frame, QRectF(frame.rect()))
            bottom = QRectF(frame_x, text_bottom + gap, frame_w, fh)
            p.translate(0, bottom.top() + bottom.bottom())
            p.scale(1, -1)
            p.drawPixmap(bottom, frame, QRectF(frame.rect()))
            p.restore()

        base_col = QColor(154, 158, 177, 235)
        hot_col = QColor(246, 240, 232, 255)
        for i, (zh, en, action) in enumerate(self.items):
            r = self._rects[i] if i < len(self._rects) else QRectF(112, 662 + i * 64, 380, 56)
            near = clamp(1.0 - abs(i - focus))
            col = QColor(
                int(base_col.red() + (hot_col.red() - base_col.red()) * near),
                int(base_col.green() + (hot_col.green() - base_col.green()) * near),
                int(base_col.blue() + (hot_col.blue() - base_col.blue()) * near),
                int(base_col.alpha() + (hot_col.alpha() - base_col.alpha()) * near),
            )
            f = theme.font(28 + 2 * near, True, spacing=1.2)
            w = theme.metrics(f).horizontalAdvance(zh)
            theme.text(p, r.center().x() - w / 2, r.y() + 32, zh, f, col,
                       shadow=near > 0.35)
        # now playing, bottom right
        track = self.window.audio.track
        if track is not None:
            nr = QRectF(1370, 966, 460, 52)
            p.setBrush(QColor(10, 8, 7, 195))
            p.setPen(QPen(QColor(178, 144, 96, 125), 1))
            p.drawRoundedRect(nr, 6, 6)
            cov = a.cover(track, 34)
            if not cov.isNull():
                p.drawPixmap(QRectF(nr.x() + 9, nr.y() + 9, 34, 34), cov, QRectF(cov.rect()))
            tf = theme.font(14, True)
            tt = theme.metrics(tf).elidedText(track.title, Qt.TextElideMode.ElideRight, 330)
            theme.text(p, nr.x() + 52, nr.y() + 23, tt, tf, QColor(238, 226, 208, 250))
            af = theme.font(11, False)
            at = theme.metrics(af).elidedText(track.artist, Qt.TextElideMode.ElideRight, 330)
            theme.text(p, nr.x() + 52, nr.y() + 41, at, af, QColor(188, 168, 144, 225))
            theme.text(p, nr.right() - 14 - theme.width("正在播放", theme.font(10, False)),
                       nr.y() + 19, "正在播放", theme.font(10, False), QColor(206, 170, 112, 220))

        # fullscreen / restore, top right
        full = self.window.isFullScreen()
        draw_icon_button(
            p, self._fullscreen_rect,
            "restore" if full else "fullscreen",
            hover=self._hover_full, active=full,
            color=QColor(240, 226, 206, 240),
        )
        self.window.theme.text_center(
            p, self._fullscreen_rect.center().x(), self._fullscreen_rect.bottom() + 17,
            "全屏" if not full else "窗口",
            self.window.theme.font(11, False, spacing=1.0),
            QColor(214, 194, 170, 205 if self._hover_full or full else 150),
        )

        # footer
        theme = self.window.theme
        theme.text(p, 88, 1034, "F11  全屏    ·    ↑↓  选择    ·    Enter  确认",
                   theme.font(15, False), QColor(210, 194, 174, 220))
        foot = "Nientum - Op.ZERO      CG & MUSIC GALLERY      v1.0"
        theme.text(p, 1920 - 88 - theme.width(foot, theme.font(13, False)), 1034,
                   foot, theme.font(13, False), QColor(196, 178, 158, 190))

    # ------------------------------------------------------------------
    def _paint_characters(self, p: QPainter) -> None:
        a = self.window.assets
        if self._movies:
            target_h = 800
            frames = []
            for movie, left in zip(self._movies, (500, 1150)):
                frame = movie.currentPixmap()
                if frame.isNull():
                    continue
                w = int(frame.width() * target_h / max(1, frame.height()))
                frames.append((frame, left, w))
            for frame, left, w in frames:
                cx = left + w / 2
                sg = QRadialGradient(QPointF(cx, 984), w * 0.55)
                sg.setColorAt(0, QColor(0, 0, 0, 125))
                sg.setColorAt(1, QColor(0, 0, 0, 0))
                p.setBrush(sg)
                p.setPen(Qt.PenStyle.NoPen)
                p.drawEllipse(QPointF(cx, 984), w * 0.55, 26)
            for frame, left, w in frames:
                p.drawPixmap(QRectF(left, 984 - target_h, w, target_h), frame, QRectF(frame.rect()))
            return

        # fallback for a missing animated asset
        if self._tachie is None:
            self._tachie = (a.char("Liora", 840), a.char("Aleph", 840))
        liora, aleph = self._tachie
        float_y = math.sin(self.t * 0.55) * 2.5
        for pix, x in ((liora, 620), (aleph, 1080)):
            if pix.isNull():
                continue
            cx = x + pix.width() / 2
            sg = QRadialGradient(QPointF(cx, 984), pix.width() * 0.55)
            sg.setColorAt(0, QColor(0, 0, 0, 125))
            sg.setColorAt(1, QColor(0, 0, 0, 0))
            p.setBrush(sg)
            p.setPen(Qt.PenStyle.NoPen)
            p.drawEllipse(QPointF(cx, 984), pix.width() * 0.55, 26)
        if not liora.isNull():
            p.drawPixmap(int(620), int(984 - liora.height() + float_y), liora)
        if not aleph.isNull():
            p.drawPixmap(int(1080), int(984 - aleph.height() - float_y), aleph)

    # ------------------------------------------------------------------
    def mouse_move(self, pos: QPointF) -> None:
        self._layout()
        self._hover_full = hit(self._fullscreen_rect.adjusted(-6, -6, 6, 6), pos)
        if self._hover_full:
            return
        for i, r in enumerate(self._rects):
            if hit(r, pos):
                if i != self.selected:
                    self.selected = i
                    self.window.audio.play_ui("move")
                return

    def mouse_press(self, pos: QPointF, button) -> None:
        self._layout()
        if hit(self._fullscreen_rect.adjusted(-6, -6, 6, 6), pos):
            self.window.toggle_fullscreen()
            return
        for i, r in enumerate(self._rects):
            if hit(r, pos):
                self.selected = i
                self._activate(i)
                return

    def key_press(self, key: int, mods) -> bool:
        if key in (Qt.Key.Key_Up, Qt.Key.Key_W):
            self.selected = (self.selected - 1) % len(self.items)
            self.window.audio.play_ui("move")
            return True
        if key in (Qt.Key.Key_Down, Qt.Key.Key_S):
            self.selected = (self.selected + 1) % len(self.items)
            self.window.audio.play_ui("move")
            return True
        if key in (Qt.Key.Key_Return, Qt.Key.Key_Enter, Qt.Key.Key_Space):
            self._activate(self.selected)
            return True
        return False

    def _activate(self, i: int) -> None:
        self.window.audio.play_ui("select")
        action = self.items[i][2]
        if action == "freeplay":
            self.window.show_screen("freeplay", transition="curtain")
        elif action == "gallery":
            self.window.open_gallery()
        elif action == "settings":
            self.window.open_settings()
        elif action == "quit":
            self.window.quit_app()
