from __future__ import annotations

import json
import sys
from pathlib import Path

APP_DIR = Path(__file__).resolve().parent.parent
FROZEN = bool(getattr(sys, "frozen", False))
if FROZEN:
    APP_DIR = Path(sys.executable).resolve().parent

DATA_DIR = APP_DIR / "data"
if not DATA_DIR.exists():
    DATA_DIR = Path(getattr(sys, "_MEIPASS", APP_DIR)) / "data"


def _project_root() -> Path:
    lib = DATA_DIR / "library.json"
    if lib.exists():
        try:
            return Path(json.load(open(lib, encoding="utf-8"))["project_root"])
        except Exception:
            pass
    if FROZEN:
        # dist/NientumPlayer -> dist -> NientumPlayer -> project root
        return APP_DIR.parents[2]
    return APP_DIR.parent


PROJECT_ROOT = _project_root()
# Portable media lives next to the exe/entry point.  The legacy external
# project root is only a development fallback.
MEDIA_DIR = APP_DIR / "media"
if not MEDIA_DIR.exists():
    bundled_media = Path(getattr(sys, "_MEIPASS", APP_DIR)) / "media"
    MEDIA_DIR = bundled_media if bundled_media.exists() else PROJECT_ROOT
MUSIC_ROOT = MEDIA_DIR / "音乐"
CG_ROOT = MEDIA_DIR / "CG"
CG_DONE = MEDIA_DIR / "CG"
COVERS_ROOT = MEDIA_DIR / "封面"


def data(*parts: str) -> Path:
    return DATA_DIR.joinpath(*parts)


def media(relpath: str) -> Path:
    p = Path(relpath)
    return p if p.is_absolute() else MEDIA_DIR / p


def reveal(path) -> bool:
    """Open Explorer with the given file selected (Windows)."""
    import os
    import subprocess
    import sys as _sys
    p = Path(path)
    try:
        if not p.exists():
            return False
        if _sys.platform == "win32":
            # Pass the switch and the quoted path as Explorer's own command
            # line.  With the list form, subprocess quotes the whole
            # "/select,C:\..." token whenever the path contains spaces, and
            # Explorer then ignores the switch instead of selecting the file.
            subprocess.Popen(f'explorer.exe /select,"{p}"')
        elif _sys.platform == "darwin":
            subprocess.Popen(["open", "-R", str(p)])
        else:
            subprocess.Popen(["xdg-open", str(p.parent)])
        return True
    except Exception:
        try:
            if _sys.platform == "win32":
                os.startfile(str(p.parent))
                return True
        except Exception:
            pass
    return False
