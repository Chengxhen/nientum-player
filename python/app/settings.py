from __future__ import annotations

import json
import os
from pathlib import Path


def _dir() -> Path:
    base = os.environ.get("APPDATA") or str(Path.home() / "AppData" / "Roaming")
    p = Path(base) / "NientumPlayer"
    p.mkdir(parents=True, exist_ok=True)
    return p


class Settings:
    defaults = {
        "volume": 0.78,
        "muted": False,
        "viz": "halo",            # footlights / halo / off
        "blur_cg": True,
        "preview_on_select": True,
        "preview_seconds": 20,
        "device": -1,
        "geometry": "",
        "last_track": "",
        "lp_angle": 0.0,
        "fps": 120,
        "resolution": 1080,
        "title_bg": "",
        "title_track": "",
    }

    def __init__(self) -> None:
        self.path = _dir() / "settings.json"
        self.data = dict(self.defaults)
        try:
            self.data.update(json.load(open(self.path, encoding="utf-8")))
        except Exception:
            pass

    def get(self, key, default=None):
        return self.data.get(key, self.defaults.get(key, default))

    def set(self, key, value) -> None:
        self.data[key] = value
        self.save()

    def save(self) -> None:
        try:
            json.dump(self.data, open(self.path, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        except Exception:
            pass
