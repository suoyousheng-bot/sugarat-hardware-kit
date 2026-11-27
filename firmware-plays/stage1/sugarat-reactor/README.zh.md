---
description: "在 ESP32-S3 圆屏徽章上编译并运行 Sugarat Reactor 面罩遥控。"
kind: "reference"
---

# Sugarat Reactor

[English](README.md) | 中文

在反应堆上滑打开面罩，从屏幕中部下滑关闭面罩。点火时核心逐渐亮起蓝光，火花向上飘。泄能时核心逐渐变暗，余烬向下落。光效随音效启动，并在接收端回报面罩已停在目标位置后完成。徽章无法联络接收端时会继续搜索并显示连接状态。搜索期间的滑动不会在连接后补发。

从顶边下滑打开系统控制中心。在多应用模式下，点击 **Exit** 返回启动器。

在主控上左右滑，可在核心点火、分段光环和双弧流光之间循环切换，默认分段光环。两块[侧屏](../sugarat-reactor-side/README.zh.md)自动发现主控，并跟随与音效同步的亮度。

## 编译

激活 ESP-IDF 5.5.4 后，从 仓库根目录运行：

```sh
idf.py -C firmware-plays/stage1/sugarat-reactor build
```

本工程复用现有的 `../sugarat-lunch` 中的 Sugarat 板级与启动器支持和[面罩遥控逻辑](../../../esp32c3supermini/sugarat/main/manual_control.c)。应用镜像是 `build/sugarat_reactor.bin`。已有启动器的设备应将此镜像装入应用槽。生成的独立引导程序与分区表只用于独立固件。

如果接收端使用不同的 Wi-Fi STA MAC，请在 CMake 配置时设置 `SERVO_PEER_MAC`。C3 接收端（不包含在此分支）也必须配置为配对徽章的 STA MAC。应用通过信道 6 发送 ESP-NOW 数据，现有接收端会搜索已配对的徽章。A/B 位置、行程速度及灯光行为由接收端定义。

屏幕画面来自选定参考图的裁切，采用圆屏的 360 × 360 RGB565 格式。两段选定的试听 WAV 转为 16 kHz 单声道 PCM，用于徽章扬声器。
