# Wcap Next

基于 [mmozeiko/wcap](https://github.com/mmozeiko/wcap) 项目进行二次开发，在原版基础上增加了中文界面支持等改进。

Wcap Next 是一款简洁高效的 Windows 屏幕录制工具。

## 功能特性

- 通过快捷键开始/停止录制，支持显示器、窗口和区域三种录制模式
- 通过托盘图标右键菜单打开设置界面
- 视频编码支持 [H264/AVC][]、[H265/HEVC][] 或 [AV1][]，HEVC 和 AV1 支持 10-bit
- 音频编码支持 [AAC][] 或 [FLAC][]
- 窗口录制可捕获完整窗口区域（含标题栏/边框）或仅内容区域
- 窗口录制可捕获**应用程序本地音频**，不包含其他系统/进程音频
- 支持隐藏鼠标光标、禁用录制边框指示、保留窗口圆角
- 可限制录制时长（秒）或文件大小（MB）
- 可限制最大宽度、高度或帧率——捕获的帧将自动缩放
- 限制最大宽/高时，可执行**伽马校正缩放**
- 可选的**改进色彩转换**——调整输出 YUV 值，使亮度更接近原始 RGB 输入

## 二次开发新增功能

- **中文界面**：设置对话框、提示信息等均已汉化
- **界面优化**：使用微软雅黑 UI 字体，优化中文显示效果

## 技术细节

Wcap Next 使用 [Windows.Graphics.Capture][wgc] API（自 **Windows 10 版本 1903，2019 年 5 月更新 (19H1)** 起可用）捕获窗口或整个显示器的内容。捕获的纹理提交给 Media Foundation，通过硬件加速编码器将视频编码为 mp4 文件。使用合成器捕获和硬件加速编码器使其 CPU 和内存占用极低。

默认启用硬件编码器，可在设置中禁用。如果硬件视频编码出现问题，请确保 GPU 驱动程序已更新。禁用后将使用 [Microsoft Media Foundation H264][MSMFH264] 软件编码器。对于较旧的 GPU，建议使用软件编码器，因为其硬件编码器质量可能不佳。

音频通过 [WASAPI 环回录制][] 捕获，使用 [Microsoft Media Foundation AAC][MSMFAAC] 编码器或未公开的 Media Foundation FLAC 编码器（Windows 10 和 11 中似乎始终可用）进行编码。

录制的 mp4 文件可在设置中设为分片 mp4 格式（仅限 H264 编码）。分片 mp4 文件无需"最终化"，这意味着即使应用程序或 GPU 驱动程序崩溃，或磁盘空间不足，部分 mp4 文件仍然可以正常播放。分片 mp4 的缺点是文件稍大，且寻址速度较慢。

可通过设置对话框限制视频的最大分辨率——如果设置了最大宽度/高度的非零值，捕获的图像将按比例缩放。同样，可以降低捕获帧率以限制每秒最大帧数。设置为零将使用合成器帧率（通常为显示器刷新率）。较低的帧率可在相同码率下获得更高质量的视频，并减少 GPU 使用。如果录制时丢帧过多，请尝试降低视频分辨率和帧率。

仅在 Windows 10 版本 2004（2020 年 5 月更新，20H1）或更高版本中可禁用鼠标光标捕获。

在 Windows 11 上可禁用黄色录制边框或窗口圆角。在 Windows 11 24H2 版本中，可在仅窗口捕获模式下启用辅助窗口捕获，用于弹出窗口和工具窗口（如"打开文件"对话框）。

## HEVC 软件编码

HEVC 软件编码（CPU 编码）需要从 Windows 应用商店安装 HEVC 视频扩展，仅支持 8-bit 编码。可通过以下步骤直接下载安装包，无需使用 Windows 应用商店：

1. 打开 https://store.rg-adguard.net/
2. 搜索 `https://www.microsoft.com/store/productId/9n4wgh0z6vhq`，选择 `Retail` 通道
3. 下载并运行提供的 .appxbundle 包

## 构建

从源码构建需要安装 [Visual Studio][VS]，然后运行 `build.cmd` 即可。

## 许可证

本项目基于原 [wcap](https://github.com/mmozeiko/wcap) 项目，遵循与原项目相同的公共领域许可。

本软件为免费且无负担的软件，已发布至公共领域。

任何人都可以自由复制、修改、发布、使用、编译、销售或分发本软件，无论是源代码形式还是编译后的二进制形式，用于任何目的（商业或非商业），以任何方式。

[wcap-x64.exe]: https://raw.githubusercontent.com/wiki/mmozeiko/wcap/wcap-x64.exe
[wcap-arm64.exe]: https://raw.githubusercontent.com/wiki/mmozeiko/wcap/wcap-arm64.exe
[wgc]: https://blogs.windows.com/windowsdeveloper/2019/09/16/new-ways-to-do-screen-capture/
[MSMFH264]: https://docs.microsoft.com/en-us/windows/win32/medfound/h-264-video-encoder
[VS]: https://visualstudio.microsoft.com/vs/
[WASAPI 环回录制]: https://docs.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording
[MSMFAAC]: https://docs.microsoft.com/en-us/windows/win32/medfound/aac-encoder
[H264/AVC]: https://en.wikipedia.org/wiki/Advanced_Video_Coding
[H265/HEVC]: https://en.wikipedia.org/wiki/High_Efficiency_Video_Coding
[AV1]: https://en.wikipedia.org/wiki/AV1
[AAC]: https://en.wikipedia.org/wiki/Advanced_Audio_Coding
[FLAC]: https://en.wikipedia.org/wiki/FLAC
