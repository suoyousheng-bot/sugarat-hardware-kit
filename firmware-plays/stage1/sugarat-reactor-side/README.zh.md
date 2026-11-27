---
description: "在圆屏徽章上显示同步的 Sugarat Reactor 灯效。"
kind: "reference"
---

# Sugarat Reactor 侧屏

[English](README.md) | 中文

在两块侧屏徽章上各安装并打开本应用，在主控打开 [Sugarat Reactor](../sugarat-reactor/README.zh.md)。两块侧屏会在信道 6 自动发现主控。在主控上左右滑切换核心点火、分段光环和双弧流光。在任一侧屏上左右滑也能切换该侧样式，主控未连接时同样可用。主控下次切换样式时，两侧会再次采用主控选择。面罩合上时两块侧屏渐亮，打开时渐暗至黑屏。音效由主控播放。

侧屏连续收到同一主控的两帧状态后自动绑定，并在持续收到状态时跟随该主控。约 1.5 秒收不到状态时，侧屏渐暗至黑屏并恢复搜索。如果附近有多套独立头盔，每套可以使用不同的 `SUGARAT_LIGHTING_GROUP`。同一套主控和两块侧屏必须使用相同的组号，默认为 1。

激活 ESP-IDF 5.5.4 后，从工作区根目录运行：

```sh
idf.py -C firmware-plays/stage1/sugarat-reactor-side build
```

在有启动器的徽章上，把 `build/sugarat_reactor_side.bin` 安装到独立应用槽。从顶边下滑打开系统控制中心，点击 **Exit** 返回启动器。生成的引导程序和分区表用于独立固件。

屏幕在 PSRAM 中使用一块 360 × 360 RGB565 渲染缓冲。[素材工具](tools/prepare_art.py)把已选的核心、光环和双弧三阶段预览转换为明暗两端画面和逐像素光效相位。设备在广播间插值亮度，在本机渲染中间帧。切换样式时先淡出至黑，再淡入。
