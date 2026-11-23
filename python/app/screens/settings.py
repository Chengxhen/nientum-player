from __future__ import annotations

import time

from PySide6.QtCore import QPointF, QRectF, Qt
from PySide6.QtGui import QColor, QFontMetricsF, QPainter, QPen

from ..theme import CREAM, GOLD, MUTED
from ..widgets import clamp, draw_icon, draw_icon_button, draw_paper_button, hit


class SettingsOverlay:
    def __init__(self, window) -> None:
        self.window = window
        self._mouse = QPointF(-1, -1)
        self._fade = 0.0
        self._panel = QRectF(500, 130, 920, 830)
        self._close = QRectF(1360, 144, 36, 36)
        self._vol = QRectF(790, 306, 400, 6)
        self._drag_vol = False
        self._row_y = 290
        self._devices = list(window.audio.output_devices())
        dev = int(window.settings.get("device") or -1)
        self._dev_idx = dev if 0 <= dev < len(self._devices) else 0
        self._fps_box = QRectF(790, 719, 170, 34)
        self._fps_cap = QRectF(1008, 721, 96, 30)
        self._bg_reset = QRectF(1082, 786, 150, 34)
        self._music_reset = QRectF(1082, 840, 150, 34)
        self._save_btn = QRectF(970, 900, 180, 52)
        self._bg_reset_until = 0.0
        self._music_reset_until = 0.0
        self._save_until = 0.0
        self._fps_editing = False
        self._fps_buf = ""
        self._fps_replace = False
        self._blink_t = 0.0

    def tick(self, dt: float, now: float) -> None:
        self._fade = min(1.0, self._fade + dt / 0.16)
        self._blink_t += dt

    def _current_fps(self) -> int:
        try:
            return int(self.window.settings.get("fps", 60))
        except Exception:
            return 60

    def commit(self) -> None:
        if self._fps_editing:
            buf = self._fps_buf.strip()
            if buf:
                try:
                    v = max(0, min(9999, int(buf)))
                except Exception:
                    v = None
                if v is not None:
                    self.window.settings.set("fps", v)
                    self.window.stage.set_fps(v)
            self._fps_editing = False
            self._fps_buf = ""
        self.window.settings.save()

    # ------------------------------------------------------------------
    def _viz_rects(self) -> list[QRectF]:
        return [QRectF(790 + i * 82, 444, 72, 30) for i in range(3)]

    def _dur_rects(self) -> list[QRectF]:
        return [QRectF(790 + i * 72, 666, 62, 30) for i in range(3)]

    def _res_rects(self) -> list[QRectF]:
        return [QRectF(1146 + i * 52, 721, 48, 28) for i in range(5)]

    def _toggle_rects(self) -> list[QRectF]:
        return [QRectF(1268, 520, 52, 26), QRectF(1268, 592, 52, 26)]

    def _dev_arrows(self) -> tuple[QRectF, QRectF]:
        return QRectF(1260, 362, 30, 30), QRectF(1320, 362, 30, 30)

    def _close_btn(self) -> QRectF:
        return QRectF(1180, 900, 180, 52)

    # ------------------------------------------------------------------
    def paint(self, p: QPainter, now: float) -> None:
        theme = self.window.theme
        p.fillRect(QRectF(0, 0, 1920, 1080), QColor(0, 0, 0, int(182 * self._fade)))
        panel = self._panel
        p.setBrush(QColor(17, 13, 11, 246))
        p.setPen(QPen(QColor(174, 140, 92, 170), 1.2))
        p.drawRoundedRect(panel, 4, 4)
        p.setPen(QPen(QColor(190, 154, 102, 60), 1))
        p.drawRoundedRect(panel.adjusted(8, 8, -8, -8), 3, 3)
        theme.text(p, panel.x() + 60, panel.y() + 66, "设置", theme.font(32, True), CREAM)
        theme.text(p, panel.x() + 62, panel.y() + 96, "SETTINGS", theme.font(13, False, spacing=2.6),
                   QColor(198, 168, 128, 220))
        draw_icon_button(p, self._close, "close", self._close.contains(self._mouse), QColor(230, 216, 196, 240), 32)

        label_col = QColor(210, 194, 174, 245)
        value_col = QColor(232, 218, 198, 245)
        y = self._row_y

        # volume
        theme.text(p, panel.x() + 60, y + 14, "音量", theme.font(20, True), label_col)
        vol = self.window.audio.volume()
        vr = self._vol
        p.setPen(QPen(QColor(86, 72, 60, 240), 3, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        p.drawLine(QPointF(vr.x(), vr.center().y()), QPointF(vr.right(), vr.center().y()))
        p.setPen(QPen(QColor(214, 172, 110, 255), 3, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        p.drawLine(QPointF(vr.x(), vr.center().y()), QPointF(vr.x() + vr.width() * vol, vr.center().y()))
        p.setBrush(QColor(242, 220, 188, 255))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawEllipse(QPointF(vr.x() + vr.width() * vol, vr.center().y()), 6, 6)
        theme.text(p, vr.right() + 18, y + 14, f"{int(vol * 100):3d}%", theme.font(16, False), value_col)

        # output device
        y += 72
        theme.text(p, panel.x() + 60, y + 14, "输出设备", theme.font(20, True), label_col)
        desc = self._devices[self._dev_idx].description() if self._devices else "未检测到输出设备"
        df = theme.font(16, False)
        desc = QFontMetricsF(df).elidedText(desc, Qt.TextElideMode.ElideRight, 380)
        theme.text(p, 790, y + 14, desc, df, value_col)
        a1, a2 = self._dev_arrows()
        draw_icon_button(p, a1, "left", a1.contains(self._mouse), QColor(226, 212, 192, 235), 26)
        draw_icon_button(p, a2, "right", a2.contains(self._mouse), QColor(226, 212, 192, 235), 26)

        # visualisation
        y += 72
        theme.text(p, panel.x() + 60, y + 14, "音频可视化", theme.font(20, True), label_col)
        viz = self.window.settings.get("viz", "footlights")
        for i, (label, mode) in enumerate((("脚灯", "footlights"), ("光环", "halo"), ("关闭", "off"))):
            r = self._viz_rects()[i]
            active = viz == mode
            p.setBrush(QColor(190, 150, 94, 230) if active else QColor(16, 12, 10, 180))
            p.setPen(QPen(QColor(204, 168, 112, 200 if active else 120), 1))
            p.drawRoundedRect(r, 3, 3)
            theme.text_center(p, r.center().x(), r.y() + 21, label, theme.font(15, False),
                              QColor(32, 23, 17, 255) if active else QColor(218, 202, 180, 235))

        # blur toggle
        y += 76
        theme.text(p, panel.x() + 60, y + 14, "悬停 UI 时 CG 柔焦", theme.font(20, True), label_col)
        self._draw_toggle(p, self._toggle_rects()[0], bool(self.window.settings.get("blur_cg", True)))

        # preview toggle
        y += 72
        theme.text(p, panel.x() + 60, y + 14, "选中曲目时自动试听", theme.font(20, True), label_col)
        self._draw_toggle(p, self._toggle_rects()[1], bool(self.window.settings.get("preview_on_select", True)))

        # preview duration
        y += 72
        theme.text(p, panel.x() + 60, y + 14, "试听时长", theme.font(20, True), label_col)
        cur_sec = int(self.window.settings.get("preview_seconds") or 20)
        for i, sec in enumerate((10, 20, 30)):
            r = self._dur_rects()[i]
            active = cur_sec == sec
            p.setBrush(QColor(190, 150, 94, 230) if active else QColor(16, 12, 10, 180))
            p.setPen(QPen(QColor(204, 168, 112, 200 if active else 120), 1))
            p.drawRoundedRect(r, 3, 3)
            theme.text_center(p, r.center().x(), r.y() + 21, f"{sec} 秒", theme.font(15, False),
                              QColor(32, 23, 17, 255) if active else QColor(218, 202, 180, 235))

        # frame rate: free numeric input + unlimited
        y += 72
        theme.text(p, panel.x() + 60, y + 14, "界面帧率", theme.font(20, True), label_col)
        cur_fps = self._current_fps()
        box = self._fps_box
        hover_box = box.adjusted(-2, -2, 2, 2).contains(self._mouse)
        p.setBrush(QColor(9, 7, 5, 238 if self._fps_editing else 195))
        p.setPen(QPen(QColor(218, 178, 116, 235 if (hover_box or self._fps_editing) else 135), 1.2))
        p.drawRoundedRect(box, 5, 5)
        value = self._fps_buf if self._fps_editing else ("无上限" if cur_fps <= 0 else str(cur_fps))
        value_font = theme.font(20, False)
        theme.text(p, box.x() + 14, box.center().y() + 7, value, value_font,
                   QColor(242, 228, 206, 250) if (self._fps_editing or cur_fps > 0) else QColor(206, 178, 136, 245))
        if self._fps_editing and (int(self._blink_t * 2.4) % 2 == 0):
            tw = theme.width(value, value_font)
            cx = box.x() + 14 + tw + 2
            p.setPen(QPen(QColor(238, 198, 130, 230), 1.5))
            p.drawLine(QPointF(cx, box.y() + 8), QPointF(cx, box.bottom() - 8))
        theme.text(p, box.right() + 14, box.center().y() + 5, "fps", theme.font(13, False),
                   QColor(186, 168, 146, 235))
        cap = self._fps_cap
        cap_active = cur_fps <= 0
        cap_hover = cap.adjusted(-2, -2, 2, 2).contains(self._mouse)
        p.setBrush(QColor(190, 150, 94, 236) if cap_active else QColor(20, 15, 12, 230 if cap_hover else 185))
        p.setPen(QPen(QColor(214, 176, 116, 220 if (cap_active or cap_hover) else 110), 1))
        p.drawRoundedRect(cap, 5, 5)
        theme.text_center(p, cap.center().x(), cap.center().y() + 6, "无上限", theme.font(14, False),
                          QColor(34, 24, 18, 255) if cap_active else QColor(220, 204, 182, 240))
        theme.text(p, 1124, y + 14, "渲染", theme.font(13, False),
                   QColor(168, 150, 132, 215))
        cur_res = int(self.window.settings.get("resolution", 1080))
        for i, (val, label) in enumerate(((720, "720p"), (900, "900p"), (1080, "1080p"), (1440, "1440p"), (2160, "4K"))):
            rr = self._res_rects()[i]
            active = cur_res == val
            hover = rr.adjusted(-2, -2, 2, 2).contains(self._mouse)
            p.setBrush(QColor(190, 150, 94, 225) if active else QColor(20, 15, 12, 225 if hover else 170))
            p.setPen(QPen(QColor(214, 176, 116, 210 if (active or hover) else 105), 1))
            p.drawRoundedRect(rr, 4, 4)
            theme.text_center(p, rr.center().x(), rr.center().y() + 5, label,
                              theme.font(11, False),
                              QColor(34, 24, 18, 255) if active else QColor(218, 202, 180, 238))
        # title background reset (chosen from CG gallery)
        theme.text(p, panel.x() + 60, 804, "主界面背景", theme.font(20, True), label_col)
        custom_bg = self.window.settings.get("title_bg") or ""
        bg_name = custom_bg.replace(chr(92), "/").rsplit("/", 1)[-1] if custom_bg else "默认"
        bf = theme.font(14, False)
        bg_name = QFontMetricsF(bf).elidedText(bg_name, Qt.TextElideMode.ElideMiddle, 240)
        box = QRectF(790, 786, 262, 34)
        p.setBrush(QColor(9, 7, 5, 195))
        p.setPen(QPen(QColor(180, 150, 105, 135), 1))
        p.drawRoundedRect(box, 5, 5)
        theme.text(p, box.x() + 12, box.center().y() + 5, bg_name, bf,
                   QColor(224, 210, 190, 240))
        if self._bg_reset_until > 0 and time.monotonic() < self._bg_reset_until:
            theme.text(p, box.right() + 12, box.center().y() + 5, "已重置",
                       theme.font(12, False), QColor(206, 176, 120, 240))
        else:
            draw_paper_button(p, self._bg_reset, "重置背景", theme.font(14, False),
                              self._bg_reset.adjusted(-2, -2, 2, 2).contains(self._mouse),
                              enabled=bool(custom_bg))
        theme.text(p, panel.x() + 60, 852, "主界面音乐", theme.font(20, True), label_col)
        tid = str(self.window.settings.get("title_track") or "")
        tr = next((t for t in self.window.library.tracks if t.id == tid), None)
        music_name = tr.title if tr is not None else "默认（Liora's Song Unwithering）"
        mf = theme.font(14, False)
        music_name = theme.metrics(mf).elidedText(music_name, Qt.TextElideMode.ElideRight, 240)
        box = QRectF(790, 840, 262, 34)
        p.setBrush(QColor(9, 7, 5, 195))
        p.setPen(QPen(QColor(180, 150, 105, 135), 1))
        p.drawRoundedRect(box, 5, 5)
        theme.text(p, box.x() + 12, box.center().y() + 5, music_name, mf,
                   QColor(224, 210, 190, 240))
        if self._music_reset_until > 0 and time.monotonic() < self._music_reset_until:
            theme.text(p, box.right() + 12, box.center().y() + 5, "已重置",
                       theme.font(12, False), QColor(206, 176, 120, 240))
        else:
            draw_paper_button(p, self._music_reset, "重置音乐", theme.font(14, False),
                              self._music_reset.adjusted(-2, -2, 2, 2).contains(self._mouse),
                              enabled=bool(tid))
        theme.text(p, panel.x() + 60, 888, "保存后写入设置文件；背景/音乐也可在 CG 鉴赏里设置。",
                   theme.font(13, False), QColor(178, 160, 140, 225))
        draw_paper_button(p, self._save_btn, "保 存", theme.font(19, False),
                          self._save_btn.contains(self._mouse), primary=True)
        draw_paper_button(p, self._close_btn(), "关 闭", theme.font(20, False),
                          self._close_btn().contains(self._mouse))
        if self._save_until > 0 and time.monotonic() < self._save_until:
            theme.text(p, self._save_btn.x() + 6, self._save_btn.y() - 6,
                       "已保存", theme.font(13, False), QColor(206, 176, 120, 240))

    def _draw_toggle(self, p: QPainter, r: QRectF, on: bool) -> None:
        p.setBrush(QColor(190, 150, 94, 230) if on else QColor(42, 34, 28, 230))
        p.setPen(QPen(QColor(206, 170, 112, 220), 1))
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2)
        cx = r.right() - r.height() / 2 if on else r.x() + r.height() / 2
        p.setBrush(QColor(246, 232, 212, 255) if on else QColor(158, 144, 128, 255))
        p.setPen(Qt.PenStyle.NoPen)
        p.drawEllipse(QPointF(cx, r.center().y()), r.height() / 2 - 4, r.height() / 2 - 4)

    # ------------------------------------------------------------------
    def mouse_move(self, pos: QPointF) -> None:
        self._mouse = pos
        if self._drag_vol:
            self._set_vol(pos)

    def mouse_press(self, pos: QPointF, button) -> None:
        if button != Qt.MouseButton.LeftButton:
            return
        self._mouse = pos
        if self._fps_editing and not (self._fps_box.contains(pos) or self._fps_cap.contains(pos)):
            self.commit()
        if self._close.contains(pos) or self._close_btn().contains(pos):
            self.window.close_overlay()
            return
        if not self._panel.contains(pos):
            self.window.close_overlay()
            return
        if self._fps_box.contains(pos):
            cur = self._current_fps()
            self._fps_buf = "" if cur <= 0 else str(cur)
            self._fps_replace = True
            self._fps_editing = True
            return
        if self._fps_cap.contains(pos):
            self._fps_editing = False
            self._fps_buf = ""
            self.window.settings.set("fps", 0)
            self.window.stage.set_fps(0)
            return
        for i, rr in enumerate(self._res_rects()):
            if rr.contains(pos):
                self.window.settings.set("resolution", (720, 900, 1080, 1440, 2160)[i])
                return
        if self._music_reset.contains(pos):
            self.window.settings.set("title_track", "")
            self.window.audio.stop()
            default = next((t for t in self.window.library.tracks if "Liora's Song Unwithering" in t.title), None)
            if default is None:
                default = next((t for t in self.window.library.tracks if "Liora's Song" in t.title), None)
            if default is None and self.window.library.tracks:
                default = self.window.library.tracks[0]
            if default is not None:
                self.window.audio.play(default, 0, preview=False)
            self._music_reset_until = time.monotonic() + 2.0
            return
        if self._bg_reset.contains(pos):
            self.window.settings.set("title_bg", "")
            self._bg_reset_until = time.monotonic() + 2.0
            return
        if self._save_btn.contains(pos):
            self.commit()
            self.window.settings.save()
            self._save_until = time.monotonic() + 2.0
            return
        if hit(self._vol.adjusted(0, -14, 0, 14), pos):
            self._drag_vol = True
            self._set_vol(pos)
            return
        a1, a2 = self._dev_arrows()
        if a1.contains(pos) or a2.contains(pos):
            if self._devices:
                self._dev_idx = (self._dev_idx + (-1 if a1.contains(pos) else 1)) % len(self._devices)
                self.window.audio.set_output_device(self._dev_idx)
                self.window.settings.set("device", self._dev_idx)
            return
        for i, r in enumerate(self._viz_rects()):
            if r.contains(pos):
                self.window.settings.set("viz", ("footlights", "halo", "off")[i])
                return
        for i, r in enumerate(self._toggle_rects()):
            if r.contains(pos):
                if i == 0:
                    self.window.settings.set("blur_cg", not bool(self.window.settings.get("blur_cg", True)))
                else:
                    self.window.settings.set("preview_on_select", not bool(self.window.settings.get("preview_on_select", True)))
                return
        for i, r in enumerate(self._dur_rects()):
            if r.contains(pos):
                self.window.settings.set("preview_seconds", (10, 20, 30)[i])
                return
    def mouse_release(self, pos: QPointF, button) -> None:
        self._drag_vol = False

    def mouse_wheel(self, pos: QPointF, delta: int) -> None:
        pass

    def key_press(self, key: int, mods) -> bool:
        if self._fps_editing:
            if Qt.Key.Key_0 <= key <= Qt.Key.Key_9:
                ch = str(int(key) - int(Qt.Key.Key_0))
                if self._fps_replace:
                    self._fps_buf = ch
                    self._fps_replace = False
                elif len(self._fps_buf) < 4:
                    self._fps_buf += ch
                return True
            if key == Qt.Key.Key_Backspace:
                if self._fps_replace:
                    self._fps_buf = ""
                    self._fps_replace = False
                else:
                    self._fps_buf = self._fps_buf[:-1]
                return True
            if key in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
                self.commit()
                return True
            if key == Qt.Key.Key_Escape:
                self.commit()
                self.window.close_overlay()
                return True
            if key == Qt.Key.Key_Tab:
                self.commit()
                return True
            return True
        if key == Qt.Key.Key_Escape:
            self.window.close_overlay()
        return True

    def _set_vol(self, pos: QPointF) -> None:
        f = clamp((pos.x() - self._vol.x()) / self._vol.width())
        self.window.audio.set_volume(f)
        self.window.audio.set_muted(False)
        self.window.settings.set("volume", f)
        self.window.settings.set("muted", False)
