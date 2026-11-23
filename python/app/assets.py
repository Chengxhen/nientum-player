from __future__ import annotations

from collections import OrderedDict
from pathlib import Path

from PIL import Image, ImageFilter
from PySide6.QtCore import QSize, Qt
from PySide6.QtGui import QImage, QImageReader, QPainter, QPixmap

from . import paths


def pil_to_pixmap(im: Image.Image) -> QPixmap:
    im = im.convert("RGBA")
    data = im.tobytes("raw", "BGRA")
    qimg = QImage(data, im.width, im.height, QImage.Format.Format_ARGB32)
    return QPixmap.fromImage(qimg.copy())


def _load(path: Path, w: int | None = None, h: int | None = None,
          mode: str = "cover") -> QPixmap:
    reader = QImageReader(str(path))
    reader.setAutoTransform(True)
    size = reader.size()
    if size.isValid() and w and h:
        ow, oh = size.width(), size.height()
        if mode == "stretch":
            reader.setScaledSize(QSize(max(1, int(w)), max(1, int(h))))
        else:
            s = max(w / ow, h / oh) if mode == "cover" else min(w / ow, h / oh)
            reader.setScaledSize(QSize(max(1, int(ow * s)), max(1, int(oh * s))))
    img = reader.read()
    if img.isNull():
        return QPixmap()
    pix = QPixmap.fromImage(img)
    if w and h and (pix.width() != int(w) or pix.height() != int(h)):
        out = QPixmap(max(1, int(w)), max(1, int(h)))
        out.fill(Qt.GlobalColor.transparent)
        p = QPainter(out)
        p.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        if mode == "cover":
            sx = max(0, (pix.width() - int(w)) // 2)
            sy = max(0, (pix.height() - int(h)) // 2)
            p.drawPixmap(0, 0, int(w), int(h), pix, sx, sy, min(int(w), pix.width()), min(int(h), pix.height()))
        else:
            x = (int(w) - pix.width()) / 2
            y = (int(h) - pix.height()) / 2
            p.drawPixmap(int(x), int(y), pix)
        p.end()
        return out
    return pix


class Assets:
    def __init__(self) -> None:
        self._ui: dict[tuple, QPixmap] = {}
        self._covers: OrderedDict[str, QPixmap] = OrderedDict()
        self._thumbs: OrderedDict[str, QPixmap] = OrderedDict()
        self._full: OrderedDict[str, QPixmap] = OrderedDict()
        self._chars: dict[str, QPixmap] = {}
        self._blur: dict[str, QPixmap] = {}

    def ui(self, name: str, w: int | None = None, h: int | None = None,
           mode: str = "contain") -> QPixmap:
        key = (name, w, h, mode)
        pix = self._ui.get(key)
        if pix is None:
            pix = _load(paths.data("ui", name), w, h, mode)
            self._ui[key] = pix
        return pix

    def logo(self, w: int = 520) -> QPixmap:
        return self.ui("PZImgNIENTUM_logo.png", w, int(w * 0.62), "contain")

    def cover(self, track, size: int = 472) -> QPixmap:
        key = f"{track.id}:{size}"
        pix = self._covers.get(key)
        if pix is None:
            pix = _load(track.cover_path, size, size, "cover")
            self._covers[key] = pix
            while len(self._covers) > 16:
                self._covers.popitem(last=False)
        else:
            self._covers.move_to_end(key)
        return pix

    def thumb(self, item, w: int = 640, h: int = 360) -> QPixmap:
        key = f"{item.id}:{w}x{h}"
        pix = self._thumbs.get(key)
        if pix is None:
            pix = _load(item.thumb_path, w, h, "cover" if h <= 400 else "contain")
            self._thumbs[key] = pix
            while len(self._thumbs) > 40:
                self._thumbs.popitem(last=False)
        else:
            self._thumbs.move_to_end(key)
        return pix

    def full(self, relpath: str, max_size: int = 2600) -> QPixmap:
        p = paths.media(relpath)
        key = str(p)
        pix = self._full.get(key)
        if pix is None:
            reader = QImageReader(str(p))
            reader.setAutoTransform(True)
            sz = reader.size()
            if sz.isValid() and max(sz.width(), sz.height()) > max_size:
                s = max_size / max(sz.width(), sz.height())
                reader.setScaledSize(QSize(int(sz.width() * s), int(sz.height() * s)))
            img = reader.read()
            pix = QPixmap.fromImage(img) if not img.isNull() else QPixmap()
            self._full[key] = pix
            while len(self._full) > 5:
                self._full.popitem(last=False)
        else:
            self._full.move_to_end(key)
        return pix

    def char(self, name: str, h: int = 880) -> QPixmap:
        key = f"{name}:{h}"
        pix = self._chars.get(key)
        if pix is None:
            try:
                im = Image.open(paths.data("characters", f"{name}_0.png")).convert("RGBA")
                bbox = im.getbbox()
                if bbox:
                    im = im.crop(bbox)
                s = h / im.height
                im = im.resize((max(1, int(im.width * s)), h), Image.LANCZOS)
                pix = pil_to_pixmap(im)
            except Exception:
                pix = _load(paths.data("characters", f"{name}_0.png"), int(h * 0.786), h, "contain")
            self._chars[key] = pix
        return pix

    def blur_for(self, track) -> QPixmap:
        key = f"{track.act}_{track.act_zh}"
        pix = self._blur.get(key)
        if pix is None:
            p = paths.data("cache", "blur", f"{key}.jpg")
            if p.exists():
                pix = _load(p, 1920, 1080, "cover")
            else:
                try:
                    im = Image.open(paths.media(track.cg_rel)).convert("RGB")
                    w, h = im.size
                    s = max(960 / w, 540 / h)
                    im = im.resize((int(w * s) + 1, int(h * s) + 1), Image.LANCZOS)
                    lx, ty = (im.width - 960) // 2, (im.height - 540) // 2
                    im = im.crop((lx, ty, lx + 960, ty + 540)).filter(ImageFilter.GaussianBlur(11))
                    pix = pil_to_pixmap(im)
                except Exception:
                    pix = QPixmap()
            self._blur[key] = pix
        return pix

    def act_cg(self, track, w: int = 1920, h: int = 1080) -> QPixmap:
        key = f"cg:{track.act}:{w}x{h}"
        pix = self._full.get(key)
        if pix is None:
            pix = _load(paths.media(track.cg_rel), w, h, "cover")
            self._full[key] = pix
            while len(self._full) > 5:
                self._full.popitem(last=False)
        else:
            self._full.move_to_end(key)
        return pix
