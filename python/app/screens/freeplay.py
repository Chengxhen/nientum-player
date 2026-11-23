from __future__ import annotations

import math
import time

from PySide6.QtCore import QPointF, QRectF, Qt, QTimer
from PySide6.QtGui import QColor, QFontMetricsF, QLinearGradient, QPainter, QPainterPath, QPen, QPixmap, QRadialGradient

from ..widgets import clamp, draw_icon, draw_keycap, ease_out, hit
from ..window import Screen


class FreePlayScreen(Screen):
    name = "freeplay"

    # list geometry (logical 1920x1080)
    LIST_TOP = 274.0
    LIST_BOTTOM = 926.0
    LIST_CLIP = QRectF(48, 274, 980, 652)
    CARD_SPACING = 100.0
    VISIBLE_SLOTS = 6

    def __init__(self, window) -> None:
        super().__init__(window)
        self.t = 0.0
        self.cat_idx = 0
        self.track_idx = 0
        self.spin = float(self.window.settings.get("lp_angle") or 0.0)
        self._spin_speed = 0.0
        self._preview_gen = 0
        self._active = False
        self._suppress_preview = False
        self._hover_track = -1
        self._hover_tab = -1
        self._hover_chev = 0
        self._hover_help = ""
        self._hover_preview = ""
        self._hover_now = False
        self._preview_end_at = 0.0
        self._preview_started_at = 0.0
        self._previewing = False
        self._categories = [{"code": "all", "label": "全部", "tracks": self.window.library.tracks}]
        for c in self.window.library.chapters:
            self._categories.append({"code": c.code, "label": c.label, "tracks": self.window.library.act_tracks(c.code)})
        self._tab_start = 0.0
        self._tab_count = 8
        self._tab_drag = False
        self._tab_drag_x = 0.0
        self._tab_drag_start = 0.0
        self._tab_drag_moved = False
        self._tab_press_i = None
        self._tracks: list = []
        self._fade = 0.0
        self._list_fade = 1.0
        self._scroll = 0.0
        self._scroll_target = 0.0
        self._tab_focus = 0.0
        self._cat_anim = 1.0
        self._bg_cache = None
        self._record_title_cache: dict[tuple, str] = {}
        self._detail_return = None

    # ------------------------------------------------------------------
    def enter(self, **kwargs) -> None:
        self._active = True
        self._fade = 0.0
        self.t = 0.0
        self._suppress_preview = bool(kwargs.get("suppress_preview", False))
        track = kwargs.get("track")
        if kwargs.get("act"):
            code = kwargs["act"]
            for i, c in enumerate(self._categories):
                if c["code"] == code:
                    self.cat_idx = i
                    break
            self.track_idx = 0
        elif track is not None:
            # Returning from the player belongs to the track's chapter, not
            # always the "all tracks" tab.
            self.cat_idx = 0
            for i, c in enumerate(self._categories):
                if c["code"] == track.act:
                    self.cat_idx = i
                    break
            try:
                tracks = self._categories[self.cat_idx]["tracks"]
                self.track_idx = next((i for i, t in enumerate(tracks) if t.id == track.id), 0)
            except Exception:
                self.track_idx = 0
        else:
            last = self.window.settings.get("last_track") or ""
            if last in self.window.library.index_by_id:
                self.cat_idx = 0
                self.track_idx = self.window.library.index_by_id[last]
            else:
                self.cat_idx = 0
                self.track_idx = 0
        self._refresh_tracks()
        if self.cat_idx < self._tab_count:
            self._tab_start = 0
        else:
            self._tab_start = int(clamp(self.cat_idx - 3, 0, max(0, len(self._categories) - self._tab_count)))
        self._scroll_target = self._scroll_index()
        self._scroll = self._scroll_target
        self._list_fade = 0.0
        self._tab_focus = float(self.cat_idx)
        self._cat_anim = 0.0
        self._previewing = (self.window.audio.previewing and self.track is not None
                            and self.window.audio.track is not None
                            and self.window.audio.track.id == self.track.id)
        self.window.audio.previewEnded.connect(self._on_preview_end)
        self.window.stage.set_cursor_hidden(False)
        # The title screen's own song may hand over to an audition, and so may a
        # manually started concert that is currently paused.  A manually
        # started concert that is still playing keeps the stage.
        auto = not self._suppress_preview and bool(self.window.settings.get("preview_on_select"))
        audio = self.window.audio
        if (audio.full_mode and audio.track is not None and audio.playing
                and audio.track.id != self._title_track_id()):
            auto = False
        self._suppress_preview = False
        if auto:
            QTimer.singleShot(650, lambda: self._start_preview(self._preview_gen))

    def leave(self) -> None:
        self._active = False
        self._detail_return = None
        self._preview_gen += 1
        try:
            self.window.audio.previewEnded.disconnect(self._on_preview_end)
        except Exception:
            pass
        # A concert track survives the trip back into the player; auditions
        # and idle states are cleaned up.  The title screen stops the music.
        self.window.audio.cancel_handover(restore=True)
        if not self.window.audio.full_mode:
            self.window.audio.stop()
        self._previewing = False
        self.window.stage.set_cursor_hidden(False)
        try:
            self.window.settings.set("lp_angle", round(self.spin, 3))
        except Exception:
            pass

    # ------------------------------------------------------------------
    def _refresh_tracks(self) -> None:
        self._tracks = self._categories[self.cat_idx]["tracks"]
        if not self._tracks:
            self.track_idx = 0
        else:
            self.track_idx = self.track_idx % len(self._tracks)

    @property
    def track(self):
        if not self._tracks:
            return None
        return self._tracks[self.track_idx]

    def _category(self) -> dict:
        return self._categories[self.cat_idx]

    def _scroll_index(self) -> float:
        n = len(self._tracks)
        return float(clamp(self.track_idx - 2, 0, max(0, n - self.VISIBLE_SLOTS)))

    # ------------------------------------------------------------------
    def tick(self, dt: float, now: float) -> None:
        # Fallback if an overlay was dismissed by a path that did not call
        # close_overlay (e.g. a future viewer implementation).
        if self._detail_return is not None and self.window.overlay is None:
            self.on_overlay_closed()
        self.t += dt
        self._fade = min(1.0, self._fade + dt / 0.55)
        self._list_fade = min(1.0, self._list_fade + dt / 0.34)
        # the platter turns whenever the selected track is actually audible,
        # whether it is an audition or a full concert track that survived the
        # return from the player
        audio = self.window.audio
        turning = (self.track is not None and audio.track is not None
                   and audio.track.id == self.track.id and audio.playing)
        speed = 30.0 if turning else 0.0
        self._spin_speed += (speed - self._spin_speed) * (1 - math.exp(-dt / 0.55))
        if abs(self._spin_speed) > 0.02:
            self.spin = (self.spin - self._spin_speed * dt) % 360.0
        self._tab_focus += (self.cat_idx - self._tab_focus) * (1 - math.exp(-dt / 0.16))
        if abs(self.cat_idx - self._tab_focus) < 0.002:
            self._tab_focus = float(self.cat_idx)
        self._cat_anim = min(1.0, self._cat_anim + dt / 0.42)
        if abs(self._scroll - self._scroll_target) > 0.001:
            self._scroll += (self._scroll_target - self._scroll) * (1 - math.exp(-dt / 0.17))
            if abs(self._scroll - self._scroll_target) < 0.002:
                self._scroll = self._scroll_target

    # ------------------------------------------------------------------
    def _preview_seconds(self) -> int:
        return int(self.window.settings.get("preview_seconds") or 20)

    def _title_track_id(self) -> str:
        tid = str(self.window.settings.get("title_track") or "")
        if tid and tid in self.window.library.index_by_id:
            return tid
        for wanted in ("Liora's Song Unwithering", "Liora's Song"):
            track = next((t for t in self.window.library.tracks if wanted in t.title), None)
            if track is not None:
                return track.id
        return ""

    def _start_preview(self, gen: int | None = None, explicit: bool = False) -> None:
        if gen is not None and gen != self._preview_gen:
            return
        if not self._active:
            return
        if not explicit and not self.window.settings.get("preview_on_select"):
            return
        track = self.track
        if track is None:
            return
        audio = self.window.audio
        if audio.full_mode and audio.track is not None and audio.track.id == track.id and not explicit:
            return
        if (audio.previewing and audio.track is not None and audio.track.id == track.id
                and audio.playing and not explicit):
            return
        secs = self._preview_seconds()
        # A playing concert fades out before the audition starts; the
        # audition then fades in on the same player.
        audio.play_preview(track, 0, preview_len=secs * 1000, fade_over=0.55)
        self._previewing = True
        self._preview_started_at = time.monotonic()
        self._preview_end_at = self._preview_started_at + secs

    def _stop_preview(self) -> None:
        self._preview_gen += 1
        self.window.audio.stop_preview()
        self._previewing = False

    def _on_preview_end(self) -> None:
        if self._active:
            self._previewing = False

    def _select_track(self, idx: int, animate: bool = True) -> None:
        if not self._tracks:
            return
        idx = max(0, min(len(self._tracks) - 1, idx))
        if idx == self.track_idx:
            return
        self.track_idx = idx
        self._scroll_target = self._scroll_index()
        self._preview_gen += 1
        audio = self.window.audio
        if (audio.full_mode and audio.track is not None and audio.playing
                and audio.track.id != self._title_track_id()):
            # never interrupt a manually playing concert track for an
            # audition; a paused one may be parked while the audition runs
            return
        audio.cancel_handover(restore=True)
        if not audio.full_mode:
            if audio.previewing:
                audio.stop_preview(resume=False)
            else:
                audio.stop()
        self._previewing = False
        gen = self._preview_gen
        if self.window.settings.get("preview_on_select"):
            QTimer.singleShot(650, lambda: self._start_preview(gen))

    def _select_category(self, idx: int) -> None:
        if idx == self.cat_idx:
            return
        self.cat_idx = max(0, min(len(self._categories) - 1, idx))
        self.track_idx = 0
        self._refresh_tracks()
        if self.cat_idx < self._tab_count:
            self._tab_start = 0
        else:
            self._tab_start = int(clamp(self.cat_idx - 3, 0, max(0, len(self._categories) - self._tab_count)))
        self._scroll = 0.0
        self._scroll_target = 0.0
        self._list_fade = 0.0
        self._cat_anim = 0.0
        self._preview_gen += 1
        audio = self.window.audio
        if (audio.full_mode and audio.track is not None and audio.playing
                and audio.track.id != self._title_track_id()):
            # never interrupt a manually playing concert track for an
            # audition; a paused one may be parked while the audition runs
            return
        audio.cancel_handover(restore=True)
        if not audio.full_mode:
            if audio.previewing:
                audio.stop_preview(resume=False)
            else:
                audio.stop()
        self._previewing = False
        gen = self._preview_gen
        if self.window.settings.get("preview_on_select"):
            QTimer.singleShot(650, lambda: self._start_preview(gen))

    # ------------------------------------------------------------------
    def _tab_rects(self) -> list[tuple[int, QRectF]]:
        out = []
        slot = 100
        x0 = 70
        base = int(math.floor(self._tab_start))
        shift = (self._tab_start - base) * slot
        for j in range(self._tab_count):
            i = base + j
            if i >= len(self._categories):
                break
            out.append((i, QRectF(x0 + j * slot - shift, 150, slot - 10, 60)))
        return out

    def animating(self) -> bool:
        return (self._fade < 0.999 or self._list_fade < 0.999
                or self._cat_anim < 0.999
                or abs(self._scroll - self._scroll_target) > 0.002
                or abs(self._tab_focus - self.cat_idx) > 0.002
                or self._tab_drag)

    def _curve_offset(self, cy: float) -> float:
        t = clamp((cy - (self.LIST_TOP + 8)) / max(1.0, (self.LIST_BOTTOM - self.LIST_TOP - 16)))
        return 52.0 * math.sin(math.pi * t)

    def _card_rects(self) -> list[tuple[int, QRectF]]:
        out = []
        if not self._tracks:
            return out
        for i in range(len(self._tracks)):
            cy = 348.0 + (i - self._scroll) * self.CARD_SPACING
            if cy < self.LIST_TOP - 90 or cy > self.LIST_BOTTOM + 90:
                continue
            active = i == self.track_idx
            w, h = (922.0, 148.0) if active else (900.0, 134.0)
            x = 64.0 + (900.0 - w) / 2 + self._curve_offset(cy)
            out.append((i, QRectF(x, cy - h / 2, w, h)))
        return out

    def _help_rects(self) -> list[tuple[str, QRectF, str, str]]:
        theme = self.window.theme
        kf = theme.font(11, False)
        lf = theme.font(13, False)
        out = []
        x = 62.0
        y = 1012.0
        for action, key, label in (("title", "ESC", "返回"),
                                   ("select", "↑↓", "选择"),
                                   ("category", "←→", "分类"),
                                   ("play", "Enter", "开演"),
                                   ("detail", "Tab", "详情")):
            kw = max(26.0, theme.width(key, kf) + 14)
            lw = theme.width(label, lf)
            r = QRectF(x, y, kw + lw + 20, 32)
            out.append((action, r, key, label))
            x = r.right() + 6
        return out

    def _preview_rects(self) -> dict:
        bar = QRectF(1232, 1016, 628, 48)
        hint = QRectF(bar.x() + 16, bar.y() + 8, 310, 32)
        status = QRectF(bar.x() + 330, bar.y() + 8, 116, 32)
        durs = [QRectF(bar.x() + 456 + i * 54, bar.y() + 8, 50, 32) for i in range(3)]
        return {"bar": bar, "hint": hint, "status": status, "durs": durs}

    def _now_rect(self) -> QRectF:
        return QRectF(64, 934, 964, 54)

    # ------------------------------------------------------------------
    def _build_background(self) -> QPixmap:
        pm = QPixmap(1920, 1080)
        pm.fill(QColor(26, 22, 19))
        q = QPainter(pm)
        q.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
        q.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        a = self.window.assets
        theme = self.window.theme
        bg = a.ui("BG_Playlist.png", 1920, 1080, "cover")
        if not bg.isNull():
            q.drawPixmap(0, 0, 1920, 1080, bg)
        vig = a.ui("PZImgVignetting.png", 1920, 1080, "cover")
        if not vig.isNull():
            q.setOpacity(0.82)
            q.drawPixmap(0, 0, 1920, 1080, vig)
            q.setOpacity(1.0)
        # Black backing under the film also covers the light strip from the
        # backdrop, so the frame edge and sprocket holes line up.
        q.fillRect(QRectF(0, 0, 1920, 142), QColor(8, 6, 5))
        film = a.ui("film.png", 1920, 142, "stretch")
        if not film.isNull():
            q.drawPixmap(0, 0, 1920, 142, film)
        g = QLinearGradient(0, 100, 0, 190)
        g.setColorAt(0, QColor(8, 6, 5, 195))
        g.setColorAt(1, QColor(8, 6, 5, 0))
        q.fillRect(QRectF(0, 100, 1920, 90), g)
        theme.text(q, 56, 72, "FREE PLAY", theme.font(46, True, spacing=0.5), QColor(239, 228, 211, 252))
        theme.text(q, 332, 78, "自 由 选 曲", theme.font(20, True, spacing=1.2), QColor(198, 176, 150, 238))
        q.setPen(QPen(QColor(176, 141, 87, 195), 2))
        q.drawLine(QPointF(56, 88), QPointF(470, 88))
        q.end()
        return pm

    def paint(self, p: QPainter, now: float) -> None:
        theme = self.window.theme
        if self._bg_cache is None:
            self._bg_cache = self._build_background()
        p.drawPixmap(0, 0, 1920, 1080, self._bg_cache)

        self._paint_tabs(p)

        if self._tracks:
            n = len(self._tracks)
            label = f"曲目  {self.track_idx + 1:02d} / {n:02d}"
            f = theme.font(15, False, spacing=1.2)
            theme.text(p, 70, 248, label, f, QColor(190, 168, 142, 225))
            count = f"{len(self.window.library.tracks)} 曲 · {len(self.window.library.chapters)} 章"
            theme.text(p, 1000 - theme.width(count, f), 248, count, f, QColor(176, 156, 136, 210))
            p.setPen(QPen(QColor(160, 128, 88, 110), 1))
            p.drawLine(QPointF(70 + theme.width(label, f) + 18, 244), QPointF(1000 - theme.width(count, f) - 18, 244))

        self._paint_list(p)
        self._paint_now_playing(p)
        self._paint_record(p)
        self._paint_preview_bar(p)
        self._paint_guide(p)

        if self._fade < 1.0:
            p.fillRect(QRectF(0, 0, 1920, 1080), QColor(0, 0, 0, int(255 * (1 - self._fade) ** 2)))

    # ------------------------------------------------------------------
    @staticmethod
    def _mix_color(a: QColor, b: QColor, t: float) -> QColor:
        t = clamp(t)
        return QColor(
            int(round(a.red() + (b.red() - a.red()) * t)),
            int(round(a.green() + (b.green() - a.green()) * t)),
            int(round(a.blue() + (b.blue() - a.blue()) * t)),
            int(round(a.alpha() + (b.alpha() - a.alpha()) * t)),
        )

    def _paint_tabs(self, p: QPainter) -> None:
        theme = self.window.theme
        focus = self._tab_focus
        slot = 100
        x0 = 70
        if self._tab_start - 0.01 <= focus <= self._tab_start + self._tab_count - 0.99:
            fx = x0 + (focus - self._tab_start) * slot + (slot - 10) / 2
            pill = QRectF(fx - 43, 155, 86, 38)
            p.setBrush(QColor(22, 16, 12, 118))
            p.setPen(QPen(QColor(204, 162, 102, 88), 1))
            p.drawRoundedRect(pill, 18, 18)
            p.setPen(QPen(QColor(212, 172, 112, 235), 2.2))
            p.drawLine(QPointF(fx - 39, 197), QPointF(fx + 39, 197))
            p.setBrush(QColor(220, 178, 114, 255))
            p.setPen(Qt.PenStyle.NoPen)
            p.drawEllipse(QPointF(fx, 203), 2.8, 2.8)
        inactive = QColor(176, 162, 146, 195)
        active = QColor(242, 228, 208, 255)
        hover_col = QColor(202, 188, 168, 242)
        for i, r in self._tab_rects():
            near = clamp(1.0 - abs(i - focus))
            hover = i == self._hover_tab
            f = theme.font(19 + 4 * near, True, spacing=0.4)
            col = self._mix_color(inactive, active, near)
            if hover and near < 0.5:
                col = self._mix_color(col, hover_col, 0.7)
            label = self._categories[i]["label"]
            tw = theme.width(label, f)
            cx = r.center().x()
            theme.text(p, cx - tw / 2 + 1, r.y() + 33, label, f, QColor(0, 0, 0, 150))
            theme.text(p, cx - tw / 2, r.y() + 32, label, f, col)
        if self._tab_start > 0:
            col = QColor(238, 204, 148, 245) if self._hover_chev == -1 else QColor(210, 180, 132, 200)
            draw_icon(p, "left", QRectF(42, 164, 20, 20), col, 2)
        if self._tab_start + self._tab_count < len(self._categories):
            col = QColor(238, 204, 148, 245) if self._hover_chev == 1 else QColor(210, 180, 132, 200)
            draw_icon(p, "right", QRectF(878, 164, 20, 20), col, 2)

    # ------------------------------------------------------------------
    def _edge_fade(self, cy: float) -> float:
        d = min(cy - self.LIST_TOP, self.LIST_BOTTOM - cy)
        return clamp(d / 72.0)

    def _paint_list(self, p: QPainter) -> None:
        if not self._tracks:
            return
        p.save()
        p.setClipRect(self.LIST_CLIP)
        e = ease_out(self._cat_anim)
        if e < 0.999:
            p.translate(0, (1.0 - e) * 26.0)
        rects = self._card_rects()
        # Selected card is drawn last so the stack reads as overlapping paper.
        for i, r in sorted(rects, key=lambda ir: abs(ir[0] - self.track_idx), reverse=True):
            self._paint_card(p, i, r, self._edge_fade(r.center().y()))
        p.restore()

    def _paint_card(self, p: QPainter, i: int, r: QRectF, fade: float) -> None:
        a = self.window.assets
        theme = self.window.theme
        t = self._tracks[i]
        active = i == self.track_idx
        hover = i == self._hover_track
        rr = QRectF(r)
        if hover and not active:
            rr.translate(0, -3)
        base = self._list_fade * max(0.0, fade)
        if base <= 0.01:
            return
        p.setOpacity(base)

        if active:
            p.setBrush(QColor(214, 176, 112, 18))
            p.setPen(Qt.PenStyle.NoPen)
            p.drawRoundedRect(rr.adjusted(-11, -11, 11, 11), 12, 12)
            p.setBrush(QColor(214, 176, 112, 32))
            p.drawRoundedRect(rr.adjusted(-5, -5, 5, 5), 8, 8)
        # drop shadow
        p.setBrush(QColor(0, 0, 0, 105 if active else 62))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawRoundedRect(rr.adjusted(5, 6, 5, 8), 6, 6)
        paper = a.ui("List_active.png", int(rr.width()), int(rr.height()), "stretch")
        if paper.isNull():
            p.setBrush(QColor(226, 217, 204, 250 if active else 195))
            p.setPen(QPen(QColor(140, 120, 100, 180), 1))
            p.drawRoundedRect(rr, 4, 4)
        else:
            p.setOpacity(base * (1.0 if active else 0.9))
            p.drawPixmap(rr.topLeft(), paper)
            p.setOpacity(base)
        if active:
            p.setPen(QPen(QColor(218, 184, 126, 248), 1.8))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRoundedRect(rr.adjusted(1, 1, -1, -1), 4, 4)
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(QColor(204, 160, 100, 250))
            p.drawRect(QRectF(rr.x() + 3, rr.y() + 16, 3, rr.height() - 32))
        elif hover:
            p.setPen(QPen(QColor(218, 184, 126, 150), 1.3))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRoundedRect(rr.adjusted(1, 1, -1, -1), 4, 4)

        # rectangular chapter curtain-call thumbnail
        th, tw = 110.0, 196.0
        tr = QRectF(rr.x() + 18, rr.center().y() - th / 2, tw, th)
        p.setBrush(QColor(26, 21, 18, 235))
        p.setPen(QPen(QColor(145, 120, 92, 190), 1))
        p.drawRect(tr.adjusted(-2, -2, 2, 2))
        curtain = self.window.library.curtain_preview.get(t.act)
        thumb = a.thumb(curtain, int(tw), int(th)) if curtain is not None else None
        if thumb is not None and not thumb.isNull():
            p.setOpacity(base * (1.0 if active else 0.84))
            p.drawPixmap(tr, thumb, QRectF(thumb.rect()))
            p.setOpacity(base)
        else:
            p.fillRect(tr, QColor(46, 36, 30, 255))
        p.setPen(QPen(QColor(210, 174, 116, 165 if active else 95), 1))
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.drawRect(tr)
        if active:
            p.setPen(QPen(QColor(228, 192, 132, 230), 2))
            p.drawLine(QPointF(tr.x(), tr.y()), QPointF(tr.x() + 26, tr.y()))

        alpha = 255 if active else 192
        tx = tr.right() + 26
        title_font = theme.font(28 if active else 24, True)
        title_w = max(120, rr.width() - (tx - rr.x()) - 176)
        title = theme.metrics(title_font).elidedText(t.title, Qt.TextElideMode.ElideRight, title_w)
        theme.text(p, tx, rr.center().y() - 3, title, title_font, QColor(78, 62, 55, alpha))
        afont = theme.font(17 if active else 15, False)
        artist = theme.metrics(afont).elidedText(t.artist, Qt.TextElideMode.ElideRight, title_w - 10)
        theme.text(p, tx, rr.center().y() + 25, artist, afont, QColor(118, 98, 88, int(alpha * 0.92)))
        mfont = theme.font(14 if active else 13, False)
        meta = f"{t.act_label} · {t.act_zh}      {t.duration_str}"
        theme.text(p, tx, rr.center().y() + 48, meta, mfont, QColor(150, 118, 88, int(alpha * 0.92)))

        right = rr.right() - 22
        if active and t.original:
            badge = a.ui("label_original.png", 100, 33, "contain")
            if not badge.isNull():
                p.drawPixmap(QRectF(right - 100, rr.center().y() - 44, 100, 33), badge, QRectF(badge.rect()))
        state = self._audio_state(t)
        if state:
            label, sounding, is_preview = state
            if is_preview and self._previewing:
                remain = max(0, int(self._preview_end_at - time.monotonic()))
                label = f"试听中 · {remain:02d}s"
            chip_w = 148 if is_preview else 118
            chip = QRectF(right - chip_w, rr.center().y() + (19 if active else -14), chip_w, 28)
            p.setBrush(QColor(24, 18, 14, 228))
            p.setPen(QPen(QColor(206, 168, 108, 205), 1))
            p.drawRoundedRect(chip, 14, 14)
            self._draw_eq(p, chip.x() + 12, chip.center().y(), sounding, QColor(234, 192, 126, 255))
            theme.text(p, chip.x() + 27, chip.y() + 19, label,
                       theme.font(13, False), QColor(240, 224, 200, 248))

    def _audio_state(self, t) -> tuple[str, bool, bool] | None:
        audio = self.window.audio
        if audio.track is None or audio.track.id != t.id:
            return None
        if audio.previewing:
            return ("试听中", audio.playing, True)
        if audio.full_mode:
            return ("正在播放" if audio.playing else "已暂停", audio.playing, False)
        return None

    def _draw_eq(self, p: QPainter, x: float, cy: float, sounding: bool, color: QColor) -> None:
        p.setPen(Qt.PenStyle.NoPen)
        for j in range(3):
            if sounding:
                h = 5.0 + 8.0 * (0.5 + 0.5 * math.sin(self.t * 7.0 + j * 1.7))
            else:
                h = 4.0
            p.setBrush(color)
            p.drawRoundedRect(QRectF(x + j * 4.2, cy - h / 2, 2.4, h), 1.2, 1.2)

    # ------------------------------------------------------------------
    def _paint_now_playing(self, p: QPainter) -> None:
        audio = self.window.audio
        if audio.track is None or not audio.full_mode:
            return
        t = audio.track
        theme = self.window.theme
        r = self._now_rect()
        hover = self._hover_now
        p.setBrush(QColor(16, 12, 9, 214 if not hover else 232))
        p.setPen(QPen(QColor(206, 168, 108, 150 if hover else 105), 1.2))
        p.drawRoundedRect(r, 8, 8)
        # golden spine
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(206, 164, 100, 235))
        p.drawRoundedRect(QRectF(r.x() + 12, r.y() + 10, 3, r.height() - 20), 1.5, 1.5)
        self._draw_eq(p, r.x() + 26, r.center().y(), audio.playing, QColor(235, 192, 126, 255))
        state_font = theme.font(12, False, spacing=0.8)
        theme.text(p, r.x() + 46, r.y() + 35, "正在播放" if audio.playing else "已暂停", state_font,
                   QColor(208, 168, 110, 245))
        title = theme.metrics(theme.font(19, True)).elidedText(t.title, Qt.TextElideMode.ElideRight, 390)
        theme.text(p, r.x() + 142, r.y() + 35, title, theme.font(19, True), QColor(242, 230, 212, 250))
        sub = f"{t.act_label} · {t.act_zh}      {t.artist}"
        subf = theme.font(12, False)
        sub = theme.metrics(subf).elidedText(sub, Qt.TextElideMode.ElideRight, 270)
        theme.text(p, r.x() + 552, r.y() + 25, sub, subf, QColor(184, 164, 142, 235))
        # progress
        pr = QRectF(r.x() + 552, r.y() + 36, 300, 4)
        dur = max(1, int(t.duration * 1000))
        frac = clamp(audio.player.position() / dur)
        p.setPen(QPen(QColor(74, 62, 52, 220), 3, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        p.drawLine(QPointF(pr.x(), pr.center().y()), QPointF(pr.right(), pr.center().y()))
        p.setPen(QPen(QColor(212, 172, 112, 245), 3, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        p.drawLine(QPointF(pr.x(), pr.center().y()), QPointF(pr.x() + pr.width() * frac, pr.center().y()))
        hint = "点击回到播放"
        hf = theme.font(12, False)
        theme.text(p, r.right() - 20 - theme.width(hint, hf), r.y() + 33, hint, hf,
                   QColor(214, 184, 146, 225 if hover else 170))

    # ------------------------------------------------------------------
    def _paint_record(self, p: QPainter) -> None:
        t = self.track
        if t is None:
            return
        a = self.window.assets
        theme = self.window.theme
        cx, cy = 1548.0, 506.0
        radius = 500.0
        sg = QRadialGradient(QPointF(cx + 12, cy + 24), 548)
        sg.setColorAt(0, QColor(0, 0, 0, 115))
        sg.setColorAt(0.8, QColor(0, 0, 0, 50))
        sg.setColorAt(1, QColor(0, 0, 0, 0))
        p.setBrush(sg)
        p.setPen(Qt.PenStyle.NoPen)
        p.drawEllipse(QPointF(cx + 12, cy + 24), 548, 548)

        record = a.ui("LP_original.png", 1000, 1000, "cover")
        p.save()
        p.translate(cx, cy)
        p.rotate(self.spin)
        if not record.isNull():
            p.drawPixmap(QRectF(-radius, -radius, radius * 2, radius * 2), record, QRectF(record.rect()))
        # One circular chapter cover for every track of the chapter.  The
        # source files are already framed for the record; using the first one
        # as the chapter face keeps the artwork from jumping when the
        # selection moves from song to song.
        ref = t
        try:
            chapter_tracks = self.window.library.act_tracks(t.act)
            if chapter_tracks:
                ref = chapter_tracks[0]
        except Exception:
            pass
        cover = a.cover(ref, 384)
        if not cover.isNull():
            p.drawPixmap(QRectF(-224, -224, 448, 448), cover, QRectF(cover.rect()))
        # centre hole
        p.setBrush(QColor(206, 202, 194, 240))
        p.setPen(QPen(QColor(60, 52, 46, 180), 1.5))
        p.drawEllipse(QPointF(0, 0), 11, 11)
        p.restore()

        # cover rim highlight (static, not rotated)
        p.setBrush(Qt.BrushStyle.NoBrush)
        p.setPen(QPen(QColor(236, 210, 168, 165), 1.4))
        p.drawEllipse(QPointF(cx, cy), 225, 225)
        # Soft scrim behind the title, clipped to the vinyl.  A rectangular
        # gradient here used to black out the wall under the record and leave
        # a hard edge across the grooves.
        p.save()
        clip = QPainterPath()
        clip.addEllipse(QPointF(cx, cy), radius, radius)
        p.setClipPath(clip)
        p.save()
        p.translate(cx, 920.0)
        p.scale(1.0, 0.34)
        g = QRadialGradient(QPointF(0.0, 0.0), 470.0)
        g.setColorAt(0.0, QColor(8, 6, 5, 138))
        g.setColorAt(0.55, QColor(8, 6, 5, 96))
        g.setColorAt(1.0, QColor(8, 6, 5, 0))
        p.setBrush(g)
        p.setPen(Qt.PenStyle.NoPen)
        p.drawEllipse(QPointF(0.0, 0.0), 470.0, 470.0)
        p.restore()
        p.restore()

        if t.original:
            theme.text_center(p, cx, 814, "O R I G I N A L", theme.font(13, False, spacing=2.4),
                              QColor(206, 164, 104, 245))
        title_font = theme.font(40, True)
        tk = (t.id, 860)
        title = self._record_title_cache.get(tk)
        if title is None:
            title = theme.metrics(title_font).elidedText(t.title, Qt.TextElideMode.ElideRight, 860)
            self._record_title_cache[tk] = title
        theme.text_center(p, cx, 870, title, title_font, QColor(243, 233, 218, 255), shadow=True)
        theme.text_center(p, cx, 912, t.artist, theme.font(21, False), QColor(208, 190, 168, 242), shadow=True)
        if not self._tracks:
            count = 0
            idx = 0
        else:
            if self._category()["code"] == "all":
                count = len(self.window.library.act_tracks(t.act))
                idx = t.index
            else:
                count = len(self._tracks)
                idx = self.track_idx + 1
        meta = f"{t.act_label} · {t.act_zh}      {idx:02d} / {count:02d}"
        theme.text_center(p, cx, 946, meta, theme.font(18, False), QColor(202, 166, 120, 248), shadow=True)

    # ------------------------------------------------------------------
    def _paint_preview_bar(self, p: QPainter) -> None:
        t = self.track
        if t is None:
            return
        theme = self.window.theme
        audio = self.window.audio
        rects = self._preview_rects()
        bar = rects["bar"]
        p.setBrush(QColor(13, 10, 8, 218))
        p.setPen(QPen(QColor(182, 148, 96, 128), 1))
        p.drawRoundedRect(bar, 10, 10)
        p.setPen(QPen(QColor(236, 208, 164, 22), 1))
        p.drawLine(QPointF(bar.x() + 12, bar.y() + 1.5), QPointF(bar.right() - 12, bar.y() + 1.5))

        same_full = audio.full_mode and audio.track is not None and audio.track.id == t.id
        previewing_here = audio.previewing and audio.track is not None and audio.track.id == t.id
        # the platter is the audition control now
        hr = rects["hint"]
        theme.text(p, hr.x() + 2, hr.center().y() + 6, "点击唱片播放/停止试听",
                   theme.font(12, False), QColor(206, 190, 168, 235))

        # status
        st = rects["status"]
        if previewing_here:
            remain = max(0, int(self._preview_end_at - time.monotonic()))
            state = f"试听中 · {remain:02d}s"
            s_col = QColor(238, 198, 132, 250)
        elif same_full:
            state = "正式播放中"
            s_col = QColor(214, 182, 142, 235)
        else:
            state = f"试听 {self._preview_seconds()} 秒"
            s_col = QColor(190, 172, 150, 230)
        theme.text(p, st.x() + 2, st.center().y() + 6, state, theme.font(13, False), s_col)

        # durations
        cur = self._preview_seconds()
        for i, sec in enumerate((10, 20, 30)):
            r = rects["durs"][i]
            active = cur == sec
            hov = self._hover_preview == f"dur:{i}"
            p.setBrush(QColor(194, 154, 96, 238) if active else QColor(28, 22, 17, 225 if hov else 190))
            p.setPen(QPen(QColor(212, 176, 118, 215 if active else (170 if hov else 105)), 1))
            p.drawRoundedRect(r, 7, 7)
            theme.text_center(p, r.center().x(), r.y() + 21, f"{sec}s", theme.font(13, False),
                              QColor(34, 24, 18, 255) if active else QColor(218, 202, 180, 235))

    # ------------------------------------------------------------------
    def _paint_guide(self, p: QPainter) -> None:
        theme = self.window.theme
        rects = self._help_rects()
        if not rects:
            return
        panel = QRectF(50, 1004, rects[-1][1].right() + 10 - 50, 46)
        p.setBrush(QColor(14, 11, 9, 205))
        p.setPen(QPen(QColor(168, 136, 90, 105), 1))
        p.drawRoundedRect(panel, 8, 8)
        lf = theme.font(13, False)
        for action, r, key, label in rects:
            hover = self._hover_help == action
            if hover:
                p.setBrush(QColor(238, 202, 146, 24))
                p.setPen(Qt.PenStyle.NoPen)
                p.drawRoundedRect(r.adjusted(-2, -1, 2, 1), 6, 6)
            kw = draw_keycap(p, r.x() + 4, r.y() + 6, key, theme.font(11, False), h=20)
            theme.text(p, r.x() + 10 + kw, r.y() + 21, label, lf,
                       QColor(242, 226, 204, 250) if hover else QColor(222, 208, 188, 238))

    # ------------------------------------------------------------------
    def mouse_move(self, pos: QPointF) -> None:
        if self._tab_drag:
            dx = pos.x() - self._tab_drag_x
            if abs(dx) > 3:
                self._tab_drag_moved = True
            max_start = max(0, len(self._categories) - self._tab_count)
            self._tab_start = clamp(self._tab_drag_start - dx / 100.0, 0, max_start)
            return
        self._hover_tab = -1
        self._hover_track = -1
        self._hover_chev = 0
        self._hover_help = ""
        self._hover_preview = ""
        self._hover_now = False
        for action, r, _key, _label in self._help_rects():
            if hit(r, pos):
                self._hover_help = action
                return
        rects = self._preview_rects()
        for i, r in enumerate(rects["durs"]):
            if hit(r.adjusted(-2, -4, 2, 4), pos):
                self._hover_preview = f"dur:{i}"
                return
        if self.window.audio.full_mode and hit(self._now_rect(), pos):
            self._hover_now = True
            return
        if self._tab_start > 0 and hit(QRectF(36, 158, 32, 32), pos):
            self._hover_chev = -1
        elif self._tab_start + self._tab_count < len(self._categories) and hit(QRectF(872, 158, 32, 32), pos):
            self._hover_chev = 1
        for i, r in self._tab_rects():
            if hit(r, pos):
                self._hover_tab = i
                break
        cards = sorted(self._card_rects(), key=lambda ir: 0 if ir[0] == self.track_idx else abs(ir[0] - self.track_idx))
        for i, r in cards:
            if hit(r, pos):
                self._hover_track = i
                break

    def mouse_press(self, pos: QPointF, button) -> None:
        # right-click a card: play it immediately
        if button == Qt.MouseButton.RightButton:
            for i, r in sorted(self._card_rects(), key=lambda ir: 0 if ir[0] == self.track_idx else abs(ir[0] - self.track_idx)):
                if hit(r, pos):
                    if i != self.track_idx:
                        self._select_track(i, animate=True)
                    self._play_full()
                    return
            return
        if button != Qt.MouseButton.LeftButton:
            return
        for action, r, _key, _label in self._help_rects():
            if hit(r, pos):
                self._help_action(action)
                return
        rects = self._preview_rects()
        for i, r in enumerate(rects["durs"]):
            if hit(r.adjusted(-2, -4, 2, 4), pos):
                self._set_preview_seconds((10, 20, 30)[i])
                return
        if self.window.audio.full_mode and hit(self._now_rect(), pos):
            self.window.open_player(self.window.audio.track)
            return
        if self._tab_start > 0 and hit(QRectF(36, 158, 32, 32), pos):
            self._tab_start -= 1
            return
        if self._tab_start + self._tab_count < len(self._categories) and hit(QRectF(872, 158, 32, 32), pos):
            self._tab_start += 1
            return
        if QRectF(30, 145, 880, 70).contains(pos):
            hit_i = None
            for i, r in self._tab_rects():
                if hit(r, pos):
                    hit_i = i
                    break
            self._tab_drag = True
            self._tab_drag_x = pos.x()
            self._tab_drag_start = self._tab_start
            self._tab_drag_moved = False
            self._tab_press_i = hit_i
            return
        cards = sorted(self._card_rects(), key=lambda ir: 0 if ir[0] == self.track_idx else abs(ir[0] - self.track_idx))
        for i, r in cards:
            if hit(r, pos):
                self._select_track(i, animate=True)
                return
        # clicking the record toggles the audition
        t = self.track
        if t is not None and QRectF(1048, 6, 1000, 1000).contains(pos):
            if self._previewing:
                self._stop_preview()
            else:
                self._start_preview(explicit=True)

    def mouse_release(self, pos: QPointF, button) -> None:
        if not self._tab_drag:
            return
        self._tab_drag = False
        if not self._tab_drag_moved and self._tab_press_i is not None:
            self._select_category(self._tab_press_i)
        self._tab_press_i = None
        self._tab_drag_moved = False

    def mouse_double_click(self, pos: QPointF) -> None:
        for i, r in sorted(self._card_rects(), key=lambda ir: 0 if ir[0] == self.track_idx else abs(ir[0] - self.track_idx)):
            if hit(r, pos):
                if i != self.track_idx:
                    self._select_track(i, animate=True)
                self._play_full()
                return

    def mouse_wheel(self, pos: QPointF, delta: int) -> None:
        if not delta:
            return
        step = -1 if delta > 0 else 1
        if pos.y() < 230:
            self._select_category(self.cat_idx + step)
        else:
            self._select_track(self.track_idx + step, animate=True)

    def key_press(self, key: int, mods) -> bool:
        if key in (Qt.Key.Key_Up, Qt.Key.Key_W):
            self._select_track(self.track_idx - 1, animate=True)
            return True
        if key in (Qt.Key.Key_Down, Qt.Key.Key_S):
            self._select_track(self.track_idx + 1, animate=True)
            return True
        if key in (Qt.Key.Key_Left, Qt.Key.Key_A):
            self._select_category(self.cat_idx - 1)
            return True
        if key in (Qt.Key.Key_Right, Qt.Key.Key_D):
            self._select_category(self.cat_idx + 1)
            return True
        if key in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            self._play_full()
            return True
        if key == Qt.Key.Key_Space:
            t = self.track
            if t is None:
                return True
            if self._previewing:
                self._stop_preview()
            else:
                self._start_preview(explicit=True)
            return True
        if key == Qt.Key.Key_Tab:
            self._open_detail()
            return True
        if key == Qt.Key.Key_Escape:
            self.window.show_screen("title", transition="curtain")
            return True
        return False

    # ------------------------------------------------------------------
    def _help_action(self, action: str) -> None:
        if action == "title":
            self.window.show_screen("title", transition="curtain")
        elif action == "select":
            nxt = self.track_idx + 1
            if nxt >= len(self._tracks):
                nxt = 0
            self._select_track(nxt, animate=True)
        elif action == "category":
            self._select_category((self.cat_idx + 1) % len(self._categories))
        elif action == "play":
            self._play_full()
        elif action == "detail":
            self._open_detail()

    def _set_preview_seconds(self, sec: int) -> None:
        self.window.settings.set("preview_seconds", sec)
        if self.window.audio.previewing:
            end = self._preview_started_at + sec
            self._preview_end_at = end
            self.window.audio.set_preview_remaining(int(max(0.0, end - time.monotonic()) * 1000))

    def _play_full(self) -> None:
        t = self.track
        if t is None:
            return
        self.window.settings.set("last_track", t.id)
        self._preview_gen += 1
        self.window.audio.stop()
        self._previewing = False
        self.window.open_player(t)

    def _open_detail(self) -> None:
        from .detail import TrackDetailOverlay
        t = self.track
        if t is not None:
            self._detail_return = (self.cat_idx, self.track_idx)
            self.window.overlay = TrackDetailOverlay(self.window, t)

    def on_overlay_closed(self) -> None:
        saved = self._detail_return
        if saved is None:
            return
        self._detail_return = None
        cat, track = saved
        if cat != self.cat_idx:
            self.cat_idx = max(0, min(len(self._categories) - 1, cat))
            self._refresh_tracks()
            max_start = max(0, len(self._categories) - self._tab_count)
            self._tab_start = float(int(clamp(self.cat_idx - 3, 0, max_start)))
            self._tab_focus = float(self.cat_idx)
            self._scroll = 0.0
            self._scroll_target = 0.0
        self.track_idx = max(0, min(len(self._tracks) - 1, track)) if self._tracks else 0
        self._scroll_target = self._scroll_index()
        self._scroll = self._scroll_target
