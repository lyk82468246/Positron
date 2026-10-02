# 压缩媒体断言夹具

本目录只用于 `test_host`。纯红画面与 440 Hz 纯音由
[`scripts/media_fixtures.py`](../../../scripts/media_fixtures.py) 离线生成，无下载的第三方媒体内容。
生成内容按 CC0-1.0 提供，不授予 AVC/AAC 专利许可；FFmpeg/x264 工具许可证不被重新标记。
工具版本、可执行文件哈希、每条生成参数、文件长度与 SHA-256 固定在 `manifest.json`。
仓库构建只部署固定文件，不调用桌面编码器或联网。

- `baseline-aac.mp4`：320×240、5 fps、三帧 Constrained Baseline/AVCC，AAC-LC 双声道 48 kHz。
- `main-vga.mp4`：640×480、5 fps、三帧 Main，包含 B 帧和 decoder drain。
- `aac-lc.aac`：ADTS AAC-LC 单声道 48 kHz；`aac-main.aac` 用于拒绝非 LC profile。
- `high.mp4`、`high422.mp4`、`interlaced.mp4`、`oversize.mp4`：拒绝 profile、像素布局、隔行和 656×480 超限。

验证固定输入：`python scripts/media_fixtures.py`。
只有有意更新整个夹具 pin 时运行 `python scripts/media_fixtures.py --generate --ffmpeg PATH`；
生成依赖 libx264 和原生 AAC 编码器，但它们不进入 WM6 产品。其他工具版本可能生成不同字节，
必须重新审查 manifest、profile 和设备断言，不能只替换哈希让失败通过。

断言检查实际 I420 plane/stride/像素、视频时间戳与持续时间、S16LE 内容、音频时间戳、EOF
和 seek 重播；不是实时播放、复杂画面质量、underrun 或 CPU 性能门。
