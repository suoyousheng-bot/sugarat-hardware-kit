# Sugarat Reactor — 独立固件与源码

本分支 `codex/reactor-standalone` 单独保存面罩反应堆主控与侧屏固件，以及编译它们所需的源码和共享组件。此分支拥有独立历史，不合入主分支，不创建 GitHub Release 或社区玩法，不设置“做同款”入口。

## 固件下载

| 固件 | 版本 | 文件 | 源码 |
| --- | --- | --- | --- |
| 反应堆主控 | 0.2.0 | [sugarat_reactor.bin](firmware/sugarat_reactor.bin) | [主控源码](firmware-plays/stage1/sugarat-reactor/) |
| 反应堆侧屏 | 0.1.2 | [sugarat_reactor_side.bin](firmware/sugarat_reactor_side.bin) | [侧屏源码](firmware-plays/stage1/sugarat-reactor-side/) |

固件为原工程已有的 ESP32-S3 应用镜像，保留原始字节。下载时打开文件并点击 Download raw file。[版本与 SHA-256](firmware/manifest.json)记录镜像信息；在 `firmware` 目录运行 `shasum -a 256 -c SHA256SUMS` 可核对文件。

主控上滑打开面罩，从屏幕中部下滑关闭；左右滑切换核心点火、分段光环及双弧流光。两块侧屏自动发现主控，并同步亮度和样式；面罩合上时渐亮，打开时渐暗。音效由主控播放。侧屏也可以通过左右滑单独切换样式。

## 编译

需要 ESP-IDF 5.5.4 和 ESP32-S3 工具链。激活该版本的 ESP-IDF 环境后，从本分支根目录执行：

```sh
idf.py -C firmware-plays/stage1/sugarat-reactor build
idf.py -C firmware-plays/stage1/sugarat-reactor-side build
```

产物分别为对应工程 `build/sugarat_reactor.bin` 和 `build/sugarat_reactor_side.bin`。提交的 `sdkconfig` 保存原工程配置；本分支保留原目录结构，包含本地 LVGL 8.3.11、板级支持、启动器应用接口、音频、ESP-NOW 舵机协议和手动遥控逻辑。`sugarat-lunch` 目录只提供这两个工程的依赖，不包含启动器应用工程。

主控默认接收端 STA MAC 为 `EC:D6:1B:B6:09:30`。如接收端不同，在首次配置时运行 `idf.py -C firmware-plays/stage1/sugarat-reactor -D SERVO_PEER_MAC=AA:BB:CC:DD:EE:FF build`，将示例地址替换为实际地址；C3 接收端也需配对主控的 STA MAC。两端使用信道 6。主控与侧屏的 `SUGARAT_LIGHTING_GROUP` 必须相同，默认 1。此分支只交付两个圆屏应用，不包含 C3 接收端固件。

这些 BIN 不是整机合并镜像。已有启动器的设备应将应用镜像安装到应用槽；具体写入位置取决于设备当前分区布局。此次交付未烧录设备，也未进行真机功能验收。原固件与重新编译结果的验证记录见 [VALIDATION.md](VALIDATION.md)。

## 源码校验与归属

`SOURCE_SHA256SUMS` 覆盖上传的源码、配置与资源；在根目录运行 `shasum -a 256 -c SOURCE_SHA256SUMS` 核对。目录中保留各文件原有版权声明及依赖许可，详见 [NOTICE.md](NOTICE.md)。
