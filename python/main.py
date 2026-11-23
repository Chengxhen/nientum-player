from __future__ import annotations

import sys

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QApplication

from app import applog
from app.window import NientumWindow


def main() -> int:
    applog.init()
    QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    app = QApplication(sys.argv)
    app.setApplicationName("Nientum Player")
    app.setOrganizationName("Nientum")
    app.setStyle("Fusion")
    win = NientumWindow()
    win.showFullScreen()
    applog.log("window shown")
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
