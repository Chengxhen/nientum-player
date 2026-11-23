from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path

from . import paths


@dataclass
class Chapter:
    code: str
    zh: str
    en: str
    label: str
    count: int = 0
    cg: str = ""
    variants: list[str] = field(default_factory=list)

    @property
    def title(self) -> str:
        return f"{self.label} · {self.zh}"


@dataclass
class Track:
    id: str
    act: str
    act_zh: str
    act_en: str
    act_label: str
    index: int
    title: str
    artist: str
    album: str
    trackno: str
    path: str
    duration: float
    sr: int
    bits: int
    channels: int
    cover: str
    original: bool
    cover_src: str = ""

    @property
    def abs_path(self) -> Path:
        return paths.media(self.path)

    @property
    def cover_path(self) -> Path:
        return paths.data(self.cover)

    @property
    def act_title(self) -> str:
        return f"{self.act_label} · {self.act_zh}"

    @property
    def duration_str(self) -> str:
        s = int(round(self.duration))
        return f"{s // 60:02d}:{s % 60:02d}"

    @property
    def quality(self) -> str:
        return f"{self.sr/1000:.0f}kHz / {self.bits}bit"

    @property
    def cg_rel(self) -> str:
        primary = f"CG/谢幕/{self.act}_{self.act_zh}.png"
        if paths.media(primary).exists():
            return primary
        if self.act == "ActT":
            return "CG/剧情/PZImgThankyouforplayingSmalltheater.png"
        return "CG/剧情/剧情_序章_舞台.png"


@dataclass
class GalleryItem:
    id: str
    title: str
    act: str
    variant: str
    char: str
    index: int
    path: str
    thumb: str
    w: int
    h: int

    @property
    def abs_path(self) -> Path:
        return paths.media(self.path)

    @property
    def thumb_path(self) -> Path:
        return paths.data(self.thumb)


class Library:
    def __init__(self) -> None:
        self.tracks: list[Track] = [
            Track(**t) for t in json.load(open(paths.data("tracks.json"), encoding="utf-8"))
        ]
        self.chapters: list[Chapter] = [
            Chapter(**c) for c in json.load(open(paths.data("chapters.json"), encoding="utf-8"))
        ]
        self.gallery: dict[str, list[GalleryItem]] = {
            k: [GalleryItem(**i) for i in v]
            for k, v in json.load(open(paths.data("gallery.json"), encoding="utf-8")).items()
        }
        self.by_act: dict[str, list[Track]] = {}
        for t in self.tracks:
            self.by_act.setdefault(t.act, []).append(t)
        self.chapter_by_code = {c.code: c for c in self.chapters}
        self.index_by_id = {t.id: i for i, t in enumerate(self.tracks)}
        self.act_order = {c.code: i for i, c in enumerate(self.chapters)}
        self.curtain_by_act = {}
        for item in self.gallery.get("curtain", []):
            if not item.variant and item.act:
                self.curtain_by_act.setdefault(item.act, item)
        # Chapters without a curtain-call CG still need a preview in Free Play.
        self.curtain_preview = dict(self.curtain_by_act)
        fallback_ids = {"ActT": "epilogue_universe_game", "其他": "story_005", "未分类": "story_005"}
        for code, item_id in fallback_ids.items():
            item = next((i for i in self.gallery.get("story", []) if i.id == item_id), None)
            if item is not None:
                self.curtain_preview.setdefault(code, item)

    def act_tracks(self, code: str) -> list[Track]:
        return self.by_act.get(code, [])

    def chapter(self, code: str) -> Chapter | None:
        return self.chapter_by_code.get(code)

    def next_track(self, track: Track | None, delta: int = 1) -> Track:
        if not self.tracks:
            raise RuntimeError("empty library")
        if track is None or track.id not in self.index_by_id:
            return self.tracks[0]
        i = (self.index_by_id[track.id] + delta) % len(self.tracks)
        return self.tracks[i]
