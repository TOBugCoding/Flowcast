# Flowcast

## 项目简介
基于 Windows 10，Visual Studio 2022，FFmpeg 7.1.1 开发的音频录制工具。  
Supports audio recording on Windows 10, developed with Visual Studio 2022 and FFmpeg 7.1.1.

## 功能
- 支持麦克风录制和立体声录制。  
  Supports recording from the microphone and system audio.
- 视频录制仅支持桌面录制，指定窗口可以自行修改。  
  Video recording is limited to desktop capture; window-specific capture can be modified manually.

## 配置
不同电脑需要修改麦克风配置，在 `audioRecord.cpp` 文件中：  
Different computers may require different microphone configurations, which can be adjusted in `audioRecord.cpp`:

```cpp
ret = avformat_open_input(&ifmtCtx, "audio=Microphone (Conexant ISST Audio)", ifmt, &opt);
