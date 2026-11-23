from __future__ import annotations

import time

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QFontMetricsF, QImageReader, QLinearGradient, QPainter, QPen

from ..models import GalleryItem

from .. import paths
from ..theme import CREAM, GOLD, INK, MUTED, PAPER
from ..widgets import clamp, draw_icon_button, draw_paper_button, hit


class TrackDetailOverlay:
    def __init__(self, window, track) -> None:
        self.window = window
        self.track = track
        self._mouse = QPointF(-1, -1)
        self._fade = 0.0
        self._panel = QRectF(430, 210, 1060, 660)
        self._btn_play = QRectF(760, 756, 168, 46)
        self._btn_cg = QRectF(942, 756, 168, 46)
        self._btn_close = QRectF(1124, 756, 168, 46)
        self._btn_src = QRectF(1306, 756, 168, 46)
        self._btn_title = QRectF(760, 810, 168, 46)
        self._title_set_until = 0.0
        self._close = QRectF(1440, 222, 36, 36)

    def tick(self, dt: float, now: float) -> None:
        self._fade = min(1.0, self._fade + dt / 0.18)

    def paint(self, p: QPainter, now: float) -> None:
        theme = self.window.theme
        a = self.window.assets
        p.fillRect(QRectF(0, 0, 1920, 1080), QColor(0, 0, 0, int(178 * self._fade)))
        panel = self._panel
        p.setBrush(QColor(18, 14, 12, 242))
        p.setPen(QPen(QColor(174, 140, 92, 170), 1.2))
        p.drawRoundedRect(panel, 4, 4)
        p.setPen(QPen(QColor(190, 154, 102, 70), 1))
        p.drawRoundedRect(panel.adjusted(8, 8, -8, -8), 3, 3)
        theme.text(p, panel.x() + 48, panel.y() + 64, "曲目详情", theme.font(30, True), CREAM)
        theme.text(p, panel.x() + 50, panel.y() + 92, "TRACK DETAIL", theme.font(13, False, spacing=2.5),
                   QColor(198, 168, 128, 220))
        draw_icon_button(p, self._close, "close", self._close.contains(self._mouse),
                         QColor(228, 214, 194, 240), 32)
        # cover
        cs = 280
        cx = panel.x() + 48 + cs / 2
        cy = panel.y() + 320
        p.setBrush(QColor(8, 6, 5, 220))
        p.setPen(QPen(QColor(156, 128, 94, 160), 1))
        p.drawRect(QRectF(cx - cs / 2 - 8, cy - cs / 2 - 8, cs + 16, cs + 16))
        # Match the platter face: every track in a chapter uses the first
        # track's cover, not its own square cover.
        ref = self.track
        try:
            chapter_tracks = self.window.library.act_tracks(self.track.act)
            if chapter_tracks:
                ref = chapter_tracks[0]
        except Exception:
            pass
        cover = a.cover(ref, cs)
        if not cover.isNull():
            p.drawPixmap(QRectF(cx - cs / 2, cy - cs / 2, cs, cs), cover, QRectF(cover.rect()))
        # text
        tx = panel.x() + 380
        title_font = theme.font(32, True)
        title = QFontMetricsF(title_font).elidedText(self.track.title, Qt.TextElideMode.ElideRight, 600)
        theme.text(p, tx, panel.y() + 180, title, title_font, CREAM)
        theme.text(p, tx, panel.y() + 224, self.track.artist, theme.font(18, False),
                   QColor(198, 178, 156, 240))
        rows = [
            ("章节", f"{self.track.act_label} · {self.track.act_zh}"),
            ("专辑", self.track.album or "—"),
            ("时长", self.track.duration_str),
            ("音质", f"{self.track.quality} / {self.track.channels}ch"),
            ("来源", self.track.act_en),
        ]
        y = panel.y() + 286
        for label, value in rows:
            theme.text(p, tx, y, label, theme.font(15, False), QColor(168, 148, 128, 235))
            vf = theme.font(17, False)
            val = QFontMetricsF(vf).elidedText(value, Qt.TextElideMode.ElideRight, 500)
            theme.text(p, tx + 90, y, val, vf, QColor(226, 212, 194, 240))
            p.setPen(QPen(QColor(110, 94, 80, 90), 1))
            p.drawLine(QPointF(tx, y + 16), QPointF(panel.right() - 48, y + 16))
            y += 48
        path_font = theme.font(11, False)
        path_text = QFontMetricsF(path_font).elidedText(
            self.track.path, Qt.TextElideMode.ElideMiddle, panel.right() - 48 - tx)
        theme.text(p, tx, panel.y() + 514, path_text, path_font,
                   QColor(136, 120, 104, 205))
        draw_paper_button(p, self._btn_play, "开 演", theme.font(21, False),
                          self._btn_play.contains(self._mouse), primary=True)
        draw_paper_button(p, self._btn_cg, "章节 CG", theme.font(21, False),
                          self._btn_cg.contains(self._mouse))
        draw_paper_button(p, self._btn_close, "关闭", theme.font(21, False),
                          self._btn_close.contains(self._mouse))
        draw_paper_button(p, self._btn_src, "源文件", theme.font(21, False),
                          self._btn_src.contains(self._mouse))
        draw_paper_button(p, self._btn_title, "设为主界面音乐", theme.font(17, False),
                          self._btn_title.contains(self._mouse),
                          enabled=str(self.window.settings.get("title_track") or "") != self.track.id)
        if self._title_set_until > 0 and time.monotonic() < self._title_set_until:
            theme.text(p, self._btn_title.right() + 14, self._btn_title.center().y() + 6,
                       "已设为主界面音乐", theme.font(13, False), QColor(214, 178, 118, 240))

    # ------------------------------------------------------------------
    def mouse_move(self, pos: QPointF) -> None:
        self._mouse = pos

    def mouse_press(self, pos: QPointF, button) -> None:
        if button != Qt.MouseButton.LeftButton:
            return
        self._mouse = pos
        if self._close.contains(pos) or self._btn_close.contains(pos):
            self.window.close_overlay()
            return
        if not self._panel.contains(pos):
            self.window.close_overlay()
            return
        if self._btn_play.contains(pos):
            self.window.close_overlay()
            self.window.open_player(self.track)
            return
        if self._btn_src.contains(pos):
            paths.reveal(self.track.abs_path)
            return
        if self._btn_cg.contains(pos):
            self._open_chapter_cg()
            return
        if self._btn_title.contains(pos):
            self.window.settings.set("title_track", self.track.id)
            self._title_set_until = time.monotonic() + 2.0
            return
    def _open_chapter_cg(self) -> None:
        chapter = self.window.library.chapter(self.track.act)
        fixed = {
            "ActT": "CG/谢幕/ActT_终章.png",
            "ActU": "CG/谢幕/ActU_外传.png",
            "其他": "CG/谢幕/其他_界面与菜单.png",
            "未分类": "CG/谢幕/未分类_未分类.png",
        }
        rel = fixed.get(self.track.act)
        if rel is None:
            items = self.window.library.gallery.get("curtain", [])
            for i, item in enumerate(items):
                if item.act == self.track.act and not item.variant:
                    self.window.open_viewer("curtain", i, items=items, label="谢幕 CG")
                    return
            rel = self.track.cg_rel
        if not paths.media(rel).exists():
            return
        try:
            sz = QImageReader(str(paths.media(rel))).size()
            w, h = sz.width(), sz.height()
        except Exception:
            w = h = 0
        item = GalleryItem(
            id="chapter_cg_" + self.track.act,
            title=chapter.label if chapter is not None else self.track.act_label,
            act=self.track.act,
            variant="",
            char="",
            index=0,
            path=rel,
            thumb="",
            w=w,
            h=h,
        )
        self.window.open_viewer("chapter", 0, items=[item],
                                label=(chapter.label if chapter is not None else "章节 CG"))

    def mouse_release(self, pos: QPointF, button) -> None:
        pass

    def mouse_wheel(self, pos: QPointF, delta: int) -> None:
        pass

    def key_press(self, key: int, mods) -> bool:
        if key in (Qt.Key.Key_Escape, Qt.Key.Key_Tab):
            self.window.close_overlay()
            return True
        if key in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            self.window.close_overlay()
            self.window.open_player(self.track)
            return True
        if key == Qt.Key.Key_O:
            paths.reveal(self.track.abs_path)
            return True
        return True
