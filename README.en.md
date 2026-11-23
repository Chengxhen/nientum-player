# NientumPlayer — Standalone Gallery

A standalone CG / character-art / music gallery for **Nientum - Op.ZERO**. Two implementations are kept in this repository:

- `cpp/` — **current release** (`NientumPlayer_v2.exe`, Win32 + Direct2D/D3D11 + WASAPI/Media Foundation, no third-party runtime)
- `python/` — original PySide6 version (frozen, kept for reference)

This repository contains source code only. Game CG, character art, music, fonts, and covers are **not** included; the game content is copyright QueseraGames.

## Layout

```
cpp/        full C++ source (app / audio / core / gfx)
  res/      resource script and icon
  build.bat one-step build
python/     PySide6 implementation
  main.py / app/ / NientumPlayer.spec
```

## Build the C++ release

Requires Visual Studio 2022 Build Tools with the C++ desktop workload:

```bat
cd cpp
build.bat
```

Output: `cpp\build\NientumPlayer_v2.exe`. The script reuses an existing MSVC environment or locates Build Tools via `vswhere`.

Runtime layout (assets are not shipped):

```
NientumPlayer_v2.exe
data/       UI, fonts, characters, covers, caches
media/
  CG/
  封面/
```

See `cpp/src/core/paths.cpp` for lookup order. Without assets the app starts with an empty gallery.

## Run the Python version

```bat
python -m venv .venv
.venv\Scripts\activate
pip install PySide6 Pillow numpy
python python\main.py
```

`python/app/paths.py` reads `data/` next to the entry point and falls back to the `project_root` in `data/library.json` for media.

## Release

`NientumPlayer_v2_UI_20261122.zip`: exe + `data/` (including `cache/thumbs`) + `media/` (CG and covers). No music; extract and run. Built from the `cpp/` sources in this repository.

## Copyright and takedown

This is a fan-made, unofficial project and is not affiliated with the developers or publishers of Nientum - Op.ZERO. All game content (CG, character art, music, fonts, script text) is copyright QueseraGames. Release packages are for personal, non-commercial study and sharing only. If a rights holder believes a release infringes their rights, please open an Issue in this repository and the material will be removed promptly.

## License

MIT for the code, see [LICENSE](LICENSE). Game assets are not covered.