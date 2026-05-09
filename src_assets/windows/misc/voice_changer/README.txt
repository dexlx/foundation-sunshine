Voice Changer Sidecar - 安装与使用
===================================

这个目录是 Sunshine 的"麦克风变声"功能的伴随服务（sidecar）。
Sunshine 主程序通过 UDP loopback (127.0.0.1:9876) 把每 20ms 的麦克风
PCM 帧发给本服务，本服务跑变声推理后把结果送回，再写入虚拟麦克风。

零依赖快速验证（透传模式）
--------------------------
双击 `start-voice-changer-server.bat` 即可启动透传服务，用来确认 Sunshine
和 sidecar 之间的 IPC 通畅。需要系统 PATH 里能找到 `python`（任意 3.8+）。

启用真正的 RVC 变声
-------------------
1. 双击 `install-voice-changer-deps.bat`
   - 会自动检测系统 Python 3.10/3.11，缺失则下载 embeddable Python 3.11
   - 用清华源安装 PyTorch (默认 CUDA 12.1) + RVC 依赖
   - 用 hf-mirror.com 下载 HuBERT + RMVPE 预训权重 (~354 MB)
   - 完成后会覆盖 `start-voice-changer-server.bat` 让它指向正确的 Python
2. 把你的 .pth (和可选 .index) 角色模型放到 `..\voice_changer\models\voices\`
3. 编辑 `start-voice-changer-server.bat`，把命令行参数改成：
       --backend rvc --opt model_path=<完整路径>.pth --opt index_path=<完整路径>.index
4. 在 Sunshine Web UI 的 "AudioVideo" 标签里：
   - Voice Changer Backend: ipc
   - IPC endpoint: 127.0.0.1:9876
   - Model path / Index path: 与启动参数保持一致（仅供 UI 显示，不会被
     Sunshine 主程序读取）
5. 重启 Sunshine 串流，对端开 OBS/会议软件测试虚拟麦克风音色

可选参数
--------
install-voice-changer-deps.bat -Cpu        - 装 CPU torch 而非 CUDA
install-voice-changer-deps.bat -Embed      - 强制下载 embeddable Python
install-voice-changer-deps.bat -SkipModels - 跳过预训权重下载

故障排查
--------
- "rvc package not installed" 错误 -> 重新跑 install-voice-changer-deps.bat
  并加 -Embed (说明系统 Python 是 3.12+，fairseq 装不上)
- 串流时 sidecar 收不到 UDP -> 检查 Windows 防火墙是否拦截了 127.0.0.1:9876
- 推理太慢爆音 -> CPU 跑 RVC 极慢，必须用 NVIDIA GPU

更多文档：见 ..\voice_changer\README_zh.md
