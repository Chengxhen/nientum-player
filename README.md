# NientumPlayer 独立鉴赏器

《Nientum - Op.ZERO》桌面的 CG / 立绘 / 音乐鉴赏器。仓库里有两种实现：

- `cpp/` — **当前发布版**（`NientumPlayer_v2.exe`，Win32 + Direct2D/D3D11 + WASAPI/Media Foundation，无第三方运行时依赖）
- `python/` — 初版 PySide6 实现（已冻结，保留作参考）

本仓库只包含代码。游戏 CG、立绘、音乐、字体、封面和任何提取资源均不包含；游戏内容版权归 QueseraGames 所有。

## 目录

```
cpp/
  src/            全部 C++ 源码（app / audio / core / gfx）
  res/            资源脚本与图标
  build.bat       一键构建
python/
  main.py         入口
  app/            窗口、播放器、频谱、主题、各页面
  NientumPlayer.spec
```

## 构建 C++ 版（发布版）

需要 Visual Studio 2022 Build Tools（含“使用 C++ 的桌面开发”）：

```bat
cd cpp
build.bat
```

产物在 `cpp\build\NientumPlayer_v2.exe`。脚本会优先使用已就绪的 MSVC 环境，找不到时用 `vswhere` 自动定位 Build Tools。

运行时需要把素材目录放在 exe 同级：

```
NientumPlayer_v2.exe
data/        UI、字体、角色、封面缓存等
media/
  CG/         CG 原图
  封面/       专辑封面
```

`cpp/src/core/paths.cpp` 里定义了查找顺序；缺素材时程序会启动但画廊为空。

## 运行 Python 版（历史实现）

```bat
python -m venv .venv
.venv\Scripts\activate
pip install PySide6 Pillow numpy
python python\main.py
```

`python/app/paths.py` 优先读同级 `data/`，媒体优先 `media/`，否则回退 `data/library.json` 里的 `project_root`。

## 已知限制

- 仅 Windows。C++ 版使用 Direct2D/D3D11、WASAPI 与 Media Foundation，不做跨平台抽象。
- 频谱是程序化绘制，不依赖真实 FFT。
- Python 版打包需 PyInstaller，`data/` 体积大，`media/` 建议外置。

## 发布包

Release 提供 `NientumPlayer_v2_UI_20261122.zip`：`NientumPlayer_v2.exe` + `data/`（含 `cache/thumbs` 缩略图）+ `media/`（CG、封面），不含音乐，解压即用。对应本仓库 `cpp/` 源码。

## 版权与下架声明

本项目为非官方玩家工具，与《Nientum - Op.ZERO》的开发商、发行商无隶属关系。游戏及其 CG、立绘、音乐、字体、剧本等素材版权归 QueseraGames 所有；发布包中的素材仅限个人学习交流，禁止商用、二次售卖或冒充官方。如权利人认为侵权，请通过本仓库 Issue 联系，核实后立即下架相关源码与发布物。

## 许可

代码以 MIT 许可发布，见 [LICENSE](LICENSE)。游戏素材不在许可范围内。