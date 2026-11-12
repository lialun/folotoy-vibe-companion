<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐、电池参数等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

Vibe 应用链接 `main/vibe/vibe_font.c`，来自社区的思源黑体 16px／2bpp 子集，共 4,717 字形，
显式使用 Montserrat 作为符号回退。[OFL 许可](fonts/vibe-font-license.txt)随发布许可文件一同提供。
来源、生成记录与覆盖检查见[实现记录](../docs/vibe/implementation.zh_CN.md)。

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 保留于 docs/README.md 上游硬件概览中的原始产品照片。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；保留用于上游硬件文档的浅色背景。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；上游硬件概览使用 `<picture>` 在深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

## 电池参数（battery）

[`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt)
保存万锂新能源 500 mAh 电芯的 CW2017 参数（profile2、XIL40X、2026-10-20）。
该文件是项目提供的 `喜梨_智能工卡_万锂新能源500mAh_profile2_XIL40X_20261020.txt`
的原样副本，按维护者要求保留用于本次固件更新；原文件没有附带单独的许可说明。

格式为 80 行两位十六进制数字，每行一个字节，按寄存器顺序排列。
BSP 在 `components/bsp/src/bsp_battery.c` 中嵌入这 80 字节，主机检查会验证
源文件及数组内容完全一致。文本文件 SHA-256：
`7ea96eba4634f4495a37c25ffacd54c1deaa0693b162c892d671a2ba68460919`。
解码后 80 字节 SHA-256：
`fa7365671093ef449c5bdcfb4adb279e3d275f888109f50b6b2f21e9b96220d5`。
这是电量计参数，不是充电配置，也不能通用于所有 500 mAh 电芯；详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。

## Vibe 伴侣标识与字体

[`brand/vibe-companion.svg`](brand/vibe-companion.svg) 是为本项目设计的 V 形伴侣标识，可编辑源文件按仓库 MIT 许可使用。`macos/render-icon.swift` 生成 macOS 所需各尺寸，再由 iconutil 打包为 AppIcon.icns；`macos/app/Brand.swift` 绘制菜单栏和应用内版本。应用标识不使用麦克风。

`main/vibe/font_20.c` 为 Source Han Sans SC 生成的 20 像素、2 bpp 主字体；`main/vibe/vibe_font.c` 提供 16 像素辅助文字及按键提示。12 像素字体不再链接，两种字号均使用 `fonts/vibe-symbols.txt` 的字符清单。源字体来自固定版本 LVGL 的 SourceHanSansSC-Normal.otf，转换器为 lv_font_conv 1.5.3。两种字号均遵守[字体许可](fonts/vibe-font-license.txt)，静态固件文案检查两种资源的覆盖。动态城市与电台名覆盖常用中文字库，不声称支持所有 Unicode。

## 天气图标

`icons/weather/` 保留来自 [Lucide 0.468.0](https://github.com/lucide-icons/lucide/tree/0.468.0/icons) 的 12 份 SVG 源文件，`LICENSE.txt` 保留上游 ISC／Feather MIT 声明，`SOURCE.txt` 记录版本。执行 `python3 tools/render_weather_icons.py`，使用 macOS AppKit／Swift 和 Pillow 按 192 px 渲染、缩小到 48 px，生成 `main/vibe/ui_weather_icons.inc`。12 张未压缩 A8 蒙版共占 Flash 27,648 字节，无联网下载图片，也没有持续动画。运行时顺序对应 `weather.h` 的昼夜、雨雪雾、雷暴冰雹及未知类型兜底；强度等细节使用文字表达，不必每种天气单独绘制。

`main/vibe/ui_wave.inc` 的语音状态波形为本项目原创 MIT 图形：五根圆头竖条组成 28 × 28、784 字节的静态 A8 蒙版，位于紧凑语音区左侧；设备绘制时着色，不缩放、不增加动画。

当前语音符号改为 28 × 28、784 字节 A8，番茄入口使用原创 16 × 16、256 字节计时器符号（`main/vibe/ui_timer.inc`）。两者均为本项目 MIT 素材，使用 `python3 tools/render_ui_icons.py`（Pillow）生成，在设备上静态着色，不添加动画。

应用退出图标为原创 MIT 素材：`main/vibe/ui_exit.inc` 中 16 × 16、256 字节 A8 的门与箭头蒙版，
由同一 UI 图标脚本生成。番茄占用首页功能区时，它替换下键长按提示中的计时器图标，
不缩放、不加动画，也不增加 LVGL 对象。

## Agent 适配器

agent-notify、agent-notify-dsh 与 PeonPing 的 MIT 许可保留于 `licenses/`。随 App 打包的适配器位于 `macos/agents/`，许可同时进入分发包，没有使用这些项目的声音素材。

## Vibe 伴侣项目图片

| 文件 | 尺寸 | 来源与用途 |
| --- | --- | --- |
| [宣传图](brand/vibe-companion-hero.png) | 3200 × 1760 PNG | 中文宣传封面：原创机身造型、矢量文字，以及按照现有 UI 制作的高清屏幕内容。 |
| [社区封面](brand/vibe-companion-cover.png) | 1152 × 1536 PNG | 社区玩法页使用的竖版 3:4 界面插画，图内标明插画，不包含设备实拍或外观模拟。 |
| [设备界面](brand/vibe-companion-screens.png) | 3200 × 1200 PNG | 中文功能展示，使用原生高清文字与矢量图形。仅用于文档，不改变固件字体或图片。 |
| [真实固件渲染](brand/vibe-companion-screens-actual.png) | 1600 × 600 PNG | 单独保留未经修改的 240 × 320 LVGL 渲染帧，供开发对照。 |
| [桌面端电台管理](brand/vibe-companion-desktop.png) | 980 × 690 pt，截图机上输出 2× PNG | 生产 SwiftUI 收音机页面，使用样例数据，不采集设备／网络信息。 |
| [桌面端 Agent 接入](brand/vibe-companion-desktop-agents.png) | 980 × 1100 pt，截图机上输出 2× PNG | 生产 SwiftUI Agent 页面，使用样例安装状态，不读取或修改实际 Agent 安装。 |

在 macOS 上、解析好固定的 LVGL 托管组件后重新生成：

```bash
bash tools/render_readme.sh
```

脚本运行原生固件渲染器、绘制高清宣传素材，编译不含应用入口的桌面预览并离屏渲染，不启动／更新已安装
App，不申请权限、连接设备或安装 Hook。`tools/render_readme.swift` 是可编辑排版源码，
`tools/readme_snapshot.swift` 捕获真实桌面视图。原创画面采用 MIT 许可，固件字体保留
对应许可。这些 PNG 和工具仅用于文档，不进入固件或设备运行内存。

## 保留的固件依赖许可

`tools/package_notices.py` 使用这些保留文本，因此仅构建 Mac 的环境不需要 ESP-IDF 或
已下载的托管组件。发行包声明同时包含项目、思源字体、图标、Agent 和下列依赖许可。

| 保留文件 | 来源／固定依赖 |
| --- | --- |
| `licenses/apache-2.0.txt` | ESP-IDF 5.5.3 `LICENSE`；button 4.2.0、cmake_utilities 1.1.1、esp_codec_dev 1.6.2、esp_lvgl_port 2.9.0、IDF Wi-Fi／PHY 库也随附 Apache-2.0。 |
| `licenses/esp-audio-codec-2.5.0.txt` | esp_audio_codec 2.5.0 原始 `LICENSE`，Espressif Modified MIT；第三方 MP3／AAC 署名依据[厂商版权页](https://docs.espressif.com/projects/esp-adf/en/latest/COPYRIGHT.html)。 |
| `licenses/lvgl-9.5.0.txt` | LVGL 9.5.0 `LICENCE.txt`。 |
| `licenses/lvgl-tlsf.txt` | LVGL 9.5.0 `src/stdlib/builtin/lv_tlsf.h` 中完整版权／许可块，Matthew Conte，BSD。 |
| `licenses/lvgl-sprintf.txt` | LVGL 9.5.0 `src/stdlib/builtin/LICENSE_SPRINTF.txt`。 |
| `licenses/montserrat.txt` | LVGL 9.5.0 `scripts/built_in_font/font_license/Montserrat/OFL.txt`。 |
| `licenses/fontawesome5.txt` | LVGL 9.5.0 `scripts/built_in_font/font_license/FontAwesome5/LICENSE.txt`。 |
| `licenses/cjson.txt` | ESP-IDF 5.5.3 `components/json/cJSON/LICENSE`。 |

上游来源：[ESP-IDF](https://github.com/espressif/esp-idf/tree/v5.5.3)、
[LVGL](https://github.com/lvgl/lvgl/tree/v9.5.0) 和[乐鑫组件库](https://components.espressif.com/)。
变更依赖时，同步核对许可、更新保留副本和打包标签。音频组件许可与本项目自有源码
的 MIT 许可不同。

内置电台解码器许可证为 [LGPL-2.1](licenses/ffmpeg-lgpl-2.1.txt)。FFmpeg 8.0.3 由 `macos/build-radio-helper.sh` 从校验值固定的上游源码构建，对应未修改源码压缩包与构建脚本随应用分发；生成的二进制和下载源码不提交 Git。
