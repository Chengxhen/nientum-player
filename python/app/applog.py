from __future__ import annotations

import atexit
import faulthandler
import os
import platform
import sys
import threading
import time
import traceback
from pathlib import Path

from PySide6.QtCore import QtMsgType, qInstallMessageHandler

from . import paths

_lock = threading.Lock()
_fp = None
_path: Path | None = None


def log_dir() -> Path:
    base = os.environ.get("APPDATA")
    if base:
        return Path(base) / "NientumPlayer"
    return paths.DATA_DIR


def log_path() -> Path:
    global _path
    if _path is None:
        _path = log_dir() / "crash.log"
    return _path


def log(message: str) -> None:
    if _fp is None:
        return
    stamp = time.strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{stamp}] {message}\n".encode("utf-8", "replace")
    with _lock:
        try:
            _fp.write(line)
        except Exception:
            pass


def _write_exception(exc_type, exc, tb, prefix: str = "Unhandled exception") -> None:
    message = "".join(traceback.format_exception(exc_type, exc, tb))
    log(prefix + "\n" + message.rstrip())
    try:
        if sys.stderr is not None:
            sys.stderr.write(message)
            sys.stderr.flush()
    except Exception:
        pass


def _excepthook(exc_type, exc, tb) -> None:
    _write_exception(exc_type, exc, tb)


def _thread_hook(args) -> None:
    if args.exc_type is SystemExit:
        return
    _write_exception(args.exc_type, args.exc_value, args.exc_traceback,
                     f"Unhandled exception in thread {args.thread.name if args.thread else '?'}")


def _qt_handler(mode, context, message) -> None:
    if mode == QtMsgType.QtDebugMsg:
        return
    names = {
        QtMsgType.QtInfoMsg: "INFO",
        QtMsgType.QtWarningMsg: "WARN",
        QtMsgType.QtCriticalMsg: "CRIT",
        QtMsgType.QtFatalMsg: "FATAL",
    }
    where = ""
    try:
        if context.file:
            where = f" ({context.file}:{context.line})"
    except Exception:
        pass
    log(f"Qt {names.get(mode, str(mode))}{where}: {message}")


def init() -> Path:
    global _fp
    folder = log_dir()
    try:
        folder.mkdir(parents=True, exist_ok=True)
        target = folder / "crash.log"
        if target.exists() and target.stat().st_size > 2 * 1024 * 1024:
            old = folder / "crash.old.log"
            try:
                if old.exists():
                    old.unlink()
                target.replace(old)
            except Exception:
                pass
        _fp = open(target, "ab", buffering=0)
        _path = target
    except Exception:
        _fp = None
        return paths.DATA_DIR / "error.log"

    log("=" * 60)
    log(f"startup  python={sys.version.split()[0]}  exe={sys.executable}")
    log(f"platform={platform.platform()}  frozen={getattr(sys, 'frozen', False)}")
    log(f"data_dir={paths.DATA_DIR}  argv={sys.argv}")
    faulthandler.enable(_fp, all_threads=True)
    sys.excepthook = _excepthook
    threading.excepthook = _thread_hook
    qInstallMessageHandler(_qt_handler)
    atexit.register(log, "clean exit")
    return log_path()
