from __future__ import annotations

import math

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QFontMetricsF, QLinearGradient, QPainter, QPen, QPixmap, QRadialGradient

from ..theme import CREAM, GOLD, GOLD_LIGHT, MUTED
from ..widgets import clamp, draw_icon, draw_icon_button, draw_keycap, ease_out, hit
from ..window import Screen


CATEGORIES = [
    ("curtain", "谢幕 CG"),
    ("original", "CG 原画"),
    ("story", "剧情 · 尾声"),
    ("tachie", "立绘"),
    ("diff", "差分"),
]
CG_CATS = {"curtain", "original", "story"}
CHAR_LABELS = {"Liora": "莉奥拉", "Aleph": "阿莱夫", "Alice": "爱丽丝"}


class GalleryScreen(Screen):
    name = "gallery"

    TAB_X = 70
    TAB_SLOT = 196
    TAB_Y = 150

    def __init__(self, window) -> None:
        super().__init__(window)
        self.cat = "curtain"
        self.filter = "all"
        self.page = 0
        self.selected = 0
        self.t = 0.0
        self._fade = 1.0
        self._hover_card = -1
        self._hover_tab = -1
        self._hover_filter = -1
        self._hover_page = 0
        self._hover_help = ""
        self._items: list = []
        self._tab_focus = 0.0
        self._pill_w = 120.0
        self._grid_anim = 1.0
        self._grid_dir = 0
        self._bg_cache = None
        self._title_cache: dict[tuple, str] = {}

    # ------------------------------------------------------------------
    def enter(self, category=None, index: int = 0, **kwargs) -> None:
        self.t = 0.0
        self._fade = 0.0
        if category in dict(CATEGORIES):
            self.cat = category
        self.filter = "all"
        self.page = 0
        self.selected = 0
        self._refresh()
        if index:
            self.selected = max(0, min(len(self._items) - 1, index))
            self.page = self.selected // self.per_page
        self._tab_focus = float(self._tab_index())
        self._pill_w = self._target_pill_w()
        self._grid_anim = 0.0
        self._grid_dir = 0
        self.window.stage.set_cursor_hidden(False)

    def leave(self) -> None:
        pass

    def tick(self, dt: float, now: float) -> None:
        self.t += dt
        self._fade = min(1.0, self._fade + dt / 0.58)
        self._tab_focus += (self._tab_index() - self._tab_focus) * (1 - math.exp(-dt / 0.15))
        if abs(self._tab_index() - self._tab_focus) < 0.002:
            self._tab_focus = float(self._tab_index())
        target = self._target_pill_w()
        self._pill_w += (target - self._pill_w) * (1 - math.exp(-dt / 0.18))
        self._grid_anim = min(1.0, self._grid_anim + dt / 0.42)

    # ------------------------------------------------------------------
    def _is_cg(self) -> bool:
        return self.cat in CG_CATS

    @property
    def per_page(self) -> int:
        return 6 if self._is_cg() else 10

    @property
    def cols(self) -> int:
        return 3 if self._is_cg() else 5

    def _refresh(self) -> None:
        items = self.window.library.gallery.get(self.cat, [])
        if self._is_cg():
            if self.filter != "all":
                items = [i for i in items if i.act == self.filter]
        else:
            if self.filter != "all":
                items = [i for i in items if i.char == self.filter]
        self._items = items
        self.selected = min(self.selected, max(0, len(items) - 1))

    def _pages(self) -> int:
        return max(1, math.ceil(len(self._items) / self.per_page))

    def _tab_index(self) -> int:
        for i, (key, _) in enumerate(CATEGORIES):
            if key == self.cat:
                return i
        return 0

    def _target_pill_w(self) -> float:
        label = CATEGORIES[self._tab_index()][1]
        return self.window.theme.width(label, self.window.theme.font(23, True)) + 38

    def _filter_options(self) -> list[tuple[str, str]]:
        if self._is_cg():
            codes = []
            for item in self.window.library.gallery.get(self.cat, []):
                if item.act and item.act not in codes:
                    codes.append(item.act)
            codes.sort(key=lambda c: self.window.library.act_order.get(c, 99))
            out = [("all", "全部")]
            for c in codes:
                ch = self.window.library.chapter(c)
                out.append((c, ch.label if ch else c))
            return out
        present = {item.char for item in self.window.library.gallery.get(self.cat, []) if item.char}
        out = [("all", "全部")]
        for key, label in (("Liora", "莉奥拉"), ("Aleph", "阿莱夫"), ("Alice", "爱丽丝")):
            if key in present:
                out.append((key, label))
        return out

    # ------------------------------------------------------------------
    def _tab_rects(self) -> list[tuple[int, QRectF]]:
        out = []
        for i, (_, label) in enumerate(CATEGORIES):
            out.append((i, QRectF(self.TAB_X + i * self.TAB_SLOT, self.TAB_Y, self.TAB_SLOT - 10, 60)))
        return out

    def _filter_rects(self) -> list[tuple[int, QRectF]]:
        out = []
        x = 70.0
        f = self.window.theme.font(14, False)
        for i, (key, label) in enumerate(self._filter_options()):
            w = self.window.theme.width(label, f) + 24
            out.append((i, QRectF(x, 230, w, 30)))
            x += w + 8
        return out

    def _grid_left(self) -> float:
        return 90.0 if self._is_cg() else 96.0

    def _card_rects(self) -> list[tuple[int, QRectF]]:
        out = []
        start = self.page * self.per_page
        for j in range(self.per_page):
            idx = start + j
            if idx >= len(self._items):
                break
            row, col = divmod(j, self.cols)
            if self._is_cg():
                x = self._grid_left() + col * 600
                y = 292 + row * 358
                r = QRectF(x, y, 540, 344)
            else:
                x = self._grid_left() + col * 352
                y = 286 + row * 350
                r = QRectF(x, y, 320, 330)
            if idx == self.selected or self._hover_card == idx:
                r.translate(0, -5)
            out.append((idx, r))
        return out

    def _help_rects(self) -> list[tuple[str, QRectF, str, str]]:
        theme = self.window.theme
        kf = theme.font(11, False)
        lf = theme.font(13, False)
        out = []
        x = self._grid_left() + 12.0
        y = 1012.0
        for action, key, label in (("title", "ESC", "返回"),
                                   ("cat", "←→", "分类"),
                                   ("page", "↑↓", "翻页"),
                                   ("view", "Enter", "查看"),
                                   ("full", "F", "全屏")):
            kw = max(26.0, theme.width(key, kf) + 14)
            lw = theme.width(label, lf)
            r = QRectF(x, y, kw + lw + 20, 32)
            out.append((action, r, key, label))
            x = r.right() + 6
        return out

    # ------------------------------------------------------------------
    def _build_background(self) -> QPixmap:
        pm = QPixmap(1920, 1080)
        pm.fill(QColor(28, 23, 20))
        q = QPainter(pm)
        q.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform, True)
        q.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        a = self.window.assets
        theme = self.window.theme
        bg = a.ui("Collection_bg.png", 1920, 1080, "cover")
        if not bg.isNull():
            q.drawPixmap(0, 0, 1920, 1080, bg)
        q.fillRect(QRectF(0, 0, 1920, 1080), QColor(9, 7, 6, 125))
        # Black backing under the film also covers the light strip from the
        # backdrop, so the frame edge and sprocket holes line up.
        q.fillRect(QRectF(0, 0, 1920, 142), QColor(8, 6, 5))
        film = a.ui("film.png", 1920, 142, "stretch")
        if not film.isNull():
            q.drawPixmap(0, 0, 1920, 142, film)
        g = QLinearGradient(0, 100, 0, 195)
        g.setColorAt(0, QColor(8, 6, 5, 195))
        g.setColorAt(1, QColor(8, 6, 5, 0))
        q.fillRect(QRectF(0, 100, 1920, 95), g)
        vig = a.ui("PZImgVignetting.png", 1920, 1080, "cover")
        if not vig.isNull():
            q.setOpacity(0.78)
            q.drawPixmap(0, 0, 1920, 1080, vig)
            q.setOpacity(1.0)
        theme.text(q, 56, 72, "CG GALLERY", theme.font(46, True, spacing=0.5), QColor(239, 228, 211, 252))
        theme.text(q, 332, 78, "C G 鉴 赏", theme.font(20, True, spacing=1.2), QColor(198, 176, 150, 238))
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
        self._paint_filters(p)
        self._paint_grid(p)
        self._paint_now_playing(p)
        self._paint_pager(p)
        self._paint_guide(p)

        if self._fade < 1.0:
            p.fillRect(QRectF(0, 0, 1920, 1080), QColor(0, 0, 0, int(255 * (1 - self._fade) ** 2)))

    # ------------------------------------------------------------------
    def animating(self) -> bool:
        return (self._fade < 0.999 or self._grid_anim < 0.999
                or abs(self._tab_focus - self._tab_index()) > 0.002
                or abs(self._pill_w - self._target_pill_w()) > 0.5)

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
        if 0.0 <= focus <= len(CATEGORIES) - 1 + 0.001:
            fx = self.TAB_X + focus * self.TAB_SLOT + (self.TAB_SLOT - 10) / 2
            pill = QRectF(fx - self._pill_w / 2, 156, self._pill_w, 40)
            p.setBrush(QColor(22, 16, 12, 118))
            p.setPen(QPen(QColor(204, 162, 102, 88), 1))
            p.drawRoundedRect(pill, 19, 19)
            p.setPen(QPen(QColor(212, 172, 112, 230), 2.2))
            p.drawLine(QPointF(fx - self._pill_w / 2 + 16, 199), QPointF(fx + self._pill_w / 2 - 16, 199))
            p.setBrush(QColor(220, 178, 114, 255))
            p.setPen(Qt.PenStyle.NoPen)
            p.drawEllipse(QPointF(fx, 205), 2.8, 2.8)
        inactive = QColor(176, 162, 146, 195)
        active = QColor(242, 228, 208, 255)
        hover_col = QColor(202, 188, 168, 242)
        for i, r in self._tab_rects():
            near = clamp(1.0 - abs(i - focus))
            hover = i == self._hover_tab
            f = theme.font(19 + 4 * near, True, spacing=0.3)
            col = self._mix_color(inactive, active, near)
            if hover and near < 0.5:
                col = self._mix_color(col, hover_col, 0.7)
            label = CATEGORIES[i][1]
            tw = theme.width(label, f)
            cx = r.center().x()
            theme.text(p, cx - tw / 2 + 1, r.y() + 33, label, f, QColor(0, 0, 0, 150))
            theme.text(p, cx - tw / 2, r.y() + 32, label, f, col)

    def _paint_filters(self, p: QPainter) -> None:
        theme = self.window.theme
        options = self._filter_options()
        for i, r in self._filter_rects():
            key, label = options[i]
            active = key == self.filter
            hover = i == self._hover_filter
            if active:
                p.setBrush(QColor(188, 148, 92, 235))
                p.setPen(Qt.PenStyle.NoPen)
                p.drawRoundedRect(r, 4, 4)
                col = QColor(30, 22, 16, 255)
            else:
                p.setBrush(QColor(18, 14, 12, 145 if hover else 95))
                p.setPen(QPen(QColor(166, 142, 112, 155 if hover else 105), 1))
                p.drawRoundedRect(r, 4, 4)
                col = QColor(222, 206, 186, 240)
            theme.text_center(p, r.center().x(), r.y() + 21, label, theme.font(14, False), col)
        count = f"{len(self._items)} 张"
        cf = theme.font(15, False)
        theme.text(p, 1840 - theme.width(count, cf), 250, count, cf, QColor(202, 184, 164, 235))

    def _paint_grid(self, p: QPainter) -> None:
        e = ease_out(self._grid_anim)
        p.save()
        if e < 0.999:
            if self._grid_dir:
                p.translate(self._grid_dir * (1.0 - e) * 46.0, 0)
            else:
                p.translate(0, (1.0 - e) * 24.0)
            p.setOpacity(0.55 + 0.45 * e)
        for idx, r in self._card_rects():
            self._paint_card(p, idx, r)
        p.restore()

    def _paint_card(self, p: QPainter, idx: int, r: QRectF) -> None:
        item = self._items[idx]
        theme = self.window.theme
        a = self.window.assets
        hover = idx == self._hover_card
        selected = idx == self.selected
        rr = QRectF(r)
        if hover or selected:
            p.setBrush(QColor(214, 176, 112, 18 if selected else 26))
            p.setPen(Qt.PenStyle.NoPen)
            p.drawRoundedRect(rr.adjusted(-10, -10, 10, 10), 12, 12)
            p.setBrush(QColor(214, 176, 112, 30 if hover else 18))
            p.drawRoundedRect(rr.adjusted(-5, -5, 5, 5), 8, 8)
        p.setBrush(QColor(0, 0, 0, 135 if hover else 85))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawRoundedRect(rr.adjusted(4, 7, 4, 9), 5, 5)

        if self._is_cg():
            p.setBrush(QColor(222, 212, 198, 255))
            p.setPen(QPen(QColor(158, 134, 108, 255), 1.3))
            p.drawRoundedRect(rr, 4, 4)
            art = QRectF(rr.x(), rr.y(), rr.width(), 294)
            thumb = a.thumb(item, 480, 270)
            if not thumb.isNull():
                p.save()
                p.setClipRect(art)
                p.drawPixmap(art, thumb, QRectF(thumb.rect()))
                p.restore()
            p.setPen(QPen(QColor(120, 100, 80, 210), 1.2))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRect(art)
            tk = (item.id, int(rr.width()))
            title = self._title_cache.get(tk)
            if title is None:
                title = theme.metrics(theme.font(20, True)).elidedText(
                    item.title, Qt.TextElideMode.ElideRight, rr.width() - 190)
                self._title_cache[tk] = title
            theme.text(p, rr.x() + 18, rr.y() + 326, title, theme.font(20, True), QColor(64, 52, 48, 255))
            tag = "CURTAIN CALL" if self.cat == "curtain" else dict(CATEGORIES).get(self.cat, "")
            tf = theme.font(11, False, spacing=1.4)
            theme.text(p, rr.right() - 18 - theme.width(tag, tf), rr.y() + 322, tag, tf,
                       QColor(132, 110, 92, 255))
            dims = f"{item.w} × {item.h}"
            theme.text(p, rr.right() - 18 - theme.width(dims, theme.font(11, False)),
                       rr.y() + 338, dims, theme.font(11, False), QColor(132, 110, 92, 240))
            if hover:
                chip = QRectF(art.right() - 78, art.bottom() - 34, 62, 24)
                p.setBrush(QColor(20, 15, 12, 220))
                p.setPen(QPen(QColor(210, 172, 112, 210), 1))
                p.drawRoundedRect(chip, 12, 12)
                theme.text_center(p, chip.center().x(), chip.center().y() + 5, "查看",
                                  theme.font(12, False), QColor(240, 224, 200, 245))
            border = QColor(226, 188, 130, 255) if (hover or selected) else QColor(151, 129, 105, 255)
            p.setPen(QPen(border, 2.4 if (hover or selected) else 1.3))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRoundedRect(rr, 4, 4)
        else:
            p.setBrush(QColor(221, 212, 198, 255))
            p.setPen(QPen(QColor(158, 134, 108, 255), 1.3))
            p.drawRoundedRect(rr, 4, 4)
            art = QRectF(rr.x() + 10, rr.y() + 10, rr.width() - 20, 268)
            g = QRadialGradient(QPointF(art.center().x(), art.y() + 100), art.width() * 0.82)
            g.setColorAt(0, QColor(70, 56, 48, 255))
            g.setColorAt(1, QColor(24, 19, 17, 255))
            p.setBrush(g)
            p.setPen(Qt.PenStyle.NoPen)
            p.drawRect(art)
            thumb = a.thumb(item, 300, 400)
            if not thumb.isNull():
                tw = thumb.width() * (art.height() / max(1, thumb.height()))
                th = art.height()
                p.drawPixmap(QRectF(art.center().x() - tw / 2, art.y(), tw, th), thumb, QRectF(thumb.rect()))
            title = theme.metrics(theme.font(17, True)).elidedText(
                item.title, Qt.TextElideMode.ElideRight, rr.width() - 36)
            theme.text(p, rr.x() + 16, rr.y() + 310, title, theme.font(17, True), QColor(64, 52, 48, 255))
            if hover:
                chip = QRectF(art.right() - 70, art.bottom() - 32, 56, 22)
                p.setBrush(QColor(20, 15, 12, 220))
                p.setPen(QPen(QColor(210, 172, 112, 210), 1))
                p.drawRoundedRect(chip, 11, 11)
                theme.text_center(p, chip.center().x(), chip.center().y() + 5, "查看",
                                  theme.font(11, False), QColor(240, 224, 200, 245))
            border = QColor(226, 188, 130, 255) if (hover or selected) else QColor(151, 129, 105, 255)
            p.setPen(QPen(border, 2.4 if (hover or selected) else 1.3))
            p.setBrush(Qt.BrushStyle.NoBrush)
            p.drawRoundedRect(rr, 4, 4)

    # ------------------------------------------------------------------
    def _now_rect(self) -> QRectF:
        return QRectF(620, 1002, 680, 48)

    def _paint_now_playing(self, p: QPainter) -> None:
        audio = self.window.audio
        track = audio.track
        if track is None or not audio.full_mode:
            return
        theme = self.window.theme
        a = self.window.assets
        r = self._now_rect()
        p.setBrush(QColor(14, 11, 9, 220))
        p.setPen(QPen(QColor(174, 140, 92, 135), 1))
        p.drawRoundedRect(r, 7, 7)
        cover = a.cover(track, 34)
        if not cover.isNull():
            p.drawPixmap(QRectF(r.x() + 8, r.y() + 7, 34, 34), cover, QRectF(cover.rect()))
        tf = theme.font(15, True)
        title = theme.metrics(tf).elidedText(track.title, Qt.TextElideMode.ElideRight, 430)
        theme.text(p, r.x() + 52, r.y() + 21, title, tf, QColor(240, 228, 208, 250))
        af = theme.font(11, False)
        artist = theme.metrics(af).elidedText(track.artist, Qt.TextElideMode.ElideRight, 430)
        theme.text(p, r.x() + 52, r.y() + 38, artist, af, QColor(188, 168, 144, 230))
        state = "播放中" if audio.playing else "已暂停"
        theme.text(p, r.right() - 14 - theme.width(state, theme.font(12, False)),
                   r.y() + 30, state, theme.font(12, False), QColor(216, 178, 116, 235))

    def _paint_pager(self, p: QPainter) -> None:
        if self._pages() <= 1:
            return
        theme = self.window.theme
        can_prev = self.page > 0
        can_next = self.page < self._pages() - 1
        draw_icon_button(p, QRectF(34, 560, 44, 44), "left",
                         can_prev and self._hover_page == -1,
                         QColor(234, 218, 198, 235 if can_prev else 70), 40)
        draw_icon_button(p, QRectF(1842, 560, 44, 44), "right",
                         can_next and self._hover_page == 1,
                         QColor(234, 218, 198, 235 if can_next else 70), 40)
        page = f"{self.page + 1} / {self._pages()}"
        pf = theme.font(15, False)
        theme.text(p, 1840 - theme.width(page, pf), 1040, page, pf, QColor(210, 194, 176, 235))

    def _paint_guide(self, p: QPainter) -> None:
        theme = self.window.theme
        rects = self._help_rects()
        if not rects:
            return
        gx = self._grid_left()
        panel = QRectF(gx, 1004, rects[-1][1].right() + 10 - gx, 46)
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
        self._hover_tab = -1
        self._hover_filter = -1
        self._hover_card = -1
        self._hover_page = 0
        self._hover_help = ""
        for action, r, _key, _label in self._help_rects():
            if hit(r, pos):
                self._hover_help = action
                return
        for i, r in self._tab_rects():
            if hit(r, pos):
                self._hover_tab = i
                return
        for i, r in self._filter_rects():
            if hit(r, pos):
                self._hover_filter = i
                return
        for idx, r in self._card_rects():
            if hit(r, pos):
                self._hover_card = idx
                return
        if self.page > 0 and hit(QRectF(34, 560, 44, 44), pos):
            self._hover_page = -1
        elif self.page < self._pages() - 1 and hit(QRectF(1842, 560, 44, 44), pos):
            self._hover_page = 1

    def mouse_press(self, pos: QPointF, button) -> None:
        if button != Qt.MouseButton.LeftButton:
            return
        if self.window.audio.full_mode and self.window.audio.track is not None and hit(self._now_rect(), pos):
            self.window.open_player(self.window.audio.track)
            return
        for action, r, _key, _label in self._help_rects():
            if hit(r, pos):
                self._help_action(action)
                return
        for i, r in self._tab_rects():
            if hit(r, pos):
                self._set_cat(CATEGORIES[i][0])
                return
        for i, r in self._filter_rects():
            if hit(r, pos):
                self._set_filter(self._filter_options()[i][0])
                return
        for idx, r in self._card_rects():
            if hit(r, pos):
                self.selected = idx
                self.window.open_viewer(self.cat, idx, items=self._items, label=dict(CATEGORIES)[self.cat])
                return
        if self.page > 0 and hit(QRectF(34, 560, 44, 44), pos):
            self._goto_page(self.page - 1, -1)
        elif self.page < self._pages() - 1 and hit(QRectF(1842, 560, 44, 44), pos):
            self._goto_page(self.page + 1, 1)

    def mouse_wheel(self, pos: QPointF, delta: int) -> None:
        if not delta:
            return
        d = -1 if delta > 0 else 1
        self._goto_page(self.page + d, d)

    def key_press(self, key: int, mods) -> bool:
        if key == Qt.Key.Key_Escape:
            self.window.show_screen("title", transition="curtain")
            return True
        if key in (Qt.Key.Key_Left, Qt.Key.Key_A, Qt.Key.Key_Right, Qt.Key.Key_D):
            d = -1 if key in (Qt.Key.Key_Left, Qt.Key.Key_A) else 1
            idx = (self._tab_index() + d) % len(CATEGORIES)
            self._set_cat(CATEGORIES[idx][0])
            return True
        if key in (Qt.Key.Key_Up, Qt.Key.Key_W, Qt.Key.Key_Down, Qt.Key.Key_S,
                   Qt.Key.Key_PageUp, Qt.Key.Key_PageDown):
            d = -1 if key in (Qt.Key.Key_Up, Qt.Key.Key_W, Qt.Key.Key_PageUp) else 1
            self._goto_page(self.page + d, d)
            return True
        if key in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            if self._items:
                self.window.open_viewer(self.cat, self.selected, items=self._items, label=dict(CATEGORIES)[self.cat])
            return True
        if key == Qt.Key.Key_F:
            self.window.toggle_fullscreen()
            return True
        if Qt.Key.Key_1 <= key <= Qt.Key.Key_6:
            i = key - Qt.Key.Key_1
            if 0 <= i < len(CATEGORIES):
                self._set_cat(CATEGORIES[i][0])
            return True
        return False

    # ------------------------------------------------------------------
    def _set_cat(self, cat: str) -> None:
        if cat == self.cat:
            return
        self.cat = cat
        self.filter = "all"
        self.page = 0
        self.selected = 0
        self._refresh()
        self._grid_anim = 0.0
        self._grid_dir = 0

    def _set_filter(self, code: str) -> None:
        if code == self.filter:
            return
        self.filter = code
        self.page = 0
        self.selected = 0
        self._refresh()
        self._grid_anim = 0.0
        self._grid_dir = 0

    def _goto_page(self, page: int, direction: int = 0) -> None:
        page = max(0, min(self._pages() - 1, page))
        if page == self.page:
            return
        self.page = page
        self.selected = self.page * self.per_page
        self._grid_anim = 0.0
        self._grid_dir = 1 if direction > 0 else (-1 if direction < 0 else 0)

    def _help_action(self, action: str) -> None:
        if action == "title":
            self.window.show_screen("title", transition="curtain")
        elif action == "cat":
            idx = (self._tab_index() + 1) % len(CATEGORIES)
            self._set_cat(CATEGORIES[idx][0])
        elif action == "page":
            self._goto_page(self.page + 1 if self.page < self._pages() - 1 else 0,
                            1 if self.page < self._pages() - 1 else 0)
        elif action == "view":
            if self._items:
                self.window.open_viewer(self.cat, self.selected, items=self._items,
                                        label=dict(CATEGORIES)[self.cat])
        elif action == "full":
            self.window.toggle_fullscreen()