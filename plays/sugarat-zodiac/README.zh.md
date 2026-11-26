# Zodiac Ring（星运表盘）

Sugarat 圆屏徽章的多应用玩法：由启动器装进槽位运行，从控制中心的 Exit 回到启动器。表盘外圈是一圈转动的文字，中间是当天的运势分数。

## 交互

- **待机**：外圈转动「靠近 · 看今天的星运」，中间显示星座符号。
- **揭晓**：点一下屏幕或拿起徽章（陀螺仪连续两次超过 40 dps）就会揭晓。依次播放星座连线、分数从 0 数上去、外圈换成当天的短句。20 秒无操作后回到待机。
- **换星座**：长按 1 秒，外圈的进度环走满后进入选择盘。左右拖动转盘，支持惯性；也可以直接点某个星座符号。底部的「中 / EN」切换语言。点中间确认；停手 5 秒会自动保存。
- 首次启动直接进入选择盘。顶部 50px 留给系统的下拉控制中心。

## 数据

运势完全在本地计算：同一星座、同一本地日期得到相同的结果，与设计稿网页预览用的哈希和公式逐项一致。日期取系统时钟，时区读启动器写入的 NVS `launcher_clock/utc_off`。时钟没有设置时，依次退回本应用记住的最后日期和编译日期。星座和语言保存在 NVS 命名空间 `sg_zodiac`，键为 `sign`、`lang` 和 `day`。

## 渲染

`zd_ui`（状态机）和 `zd_render`（矢量与字形光栅器）不依赖 ESP-IDF。每帧先生成绘制列表，再通过 `sugarat_frame` 按 12 行一条直接推屏，每条分成 4 列由两个核并行渲染，同时上一条在传输。动画中按 60 Hz 节拍出帧，超时则落到均匀的 30 fps；纯待机时为 30 fps。控制中心打开或有 LVGL 图层时，改走 PSRAM 图像经 LVGL 绘制。串口每 10 秒打印一行 `fps=… avg=… max=… gap=…`。

字形由 `tools/gen_assets.py` 用 Pillow 从 `assets/fonts` 中的 OFL 字体生成 A8 位图，输出 `main/zodiac/zd_assets.{h,c}`。修改文案后需要重新运行这个脚本；字体文件不随源码分发，先用 `tools/fetch_fonts.sh` 下载。

## 构建与测试

```sh
IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf5.5_py3.11_env bash -c 'source ~/esp/esp-idf/export.sh && idf.py build'
# 在电脑上回放一段脚本化会话，输出各状态的 PPM 截图
cc -std=c11 -O2 -fsanitize=address,undefined -Imain/zodiac tests/host_render.c main/zodiac/zd_ui.c main/zodiac/zd_render.c main/zodiac/zd_fortune.c main/zodiac/zd_assets.c -lm -o /tmp/zd_host && /tmp/zd_host OUTDIR
```

串口测试键：`z` 打开选择盘，`[` / `]` 转一格，`g` 切换语言，`k` 确认，`v` 模拟拿起，`h` 真实长按，`w` 左滑，`p` 截屏。

只把应用镜像写进启动器分配的槽位，不要把本工程的分区表或 bootloader 烧到装有启动器的徽章上。
