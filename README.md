# Flowcast
基于windows10，vis2022，ffmpeg7.1.1  
不同电脑需要修改麦克风配置，在audioRecord.cpp里:  
ret = avformat_open_input(&ifmtCtx, "audio=Microphone (Conexant ISST Audio)", ifmt, &opt);  
支持麦克风、立体音(电脑程序的声音)录制  
视频录制仅支持桌面录制，指定窗口可以自己修改  

