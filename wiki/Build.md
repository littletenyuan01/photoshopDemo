# 构建与运行

## 环境

- Windows 10/11（当前开发环境）
- Qt 6（本机曾用 Qt 6.11 + MinGW 64-bit）
- Qt Creator（推荐）或命令行 qmake + mingw32-make
- **可选**：OpenCV（算法）、CUDA Toolkit（GPU 加速）、CPU 并行（如 Qt Concurrent / OpenMP）

> 主编辑链路应在无 CUDA 时仍可运行；OpenCV/CUDA 以可选模块或编译开关接入后，在本页补充具体版本与路径。

## 用 Qt Creator

1. 打开 `psDemo/psDemo.pro`
2. 选择 Kit（如 Desktop Qt 6.x MinGW 64-bit）
3. 构建并运行（产物 **PSLite.exe**，由 `TARGET = PSLite` 决定）

构建产物默认在 `psDemo/build/...`，已由 `.gitignore` 忽略。

## 命令行（示例）

在 `psDemo` 目录下（路径按本机 Qt 安装调整）：

```bat
mkdir build
cd build
qmake ..\psDemo.pro
mingw32-make
```

## 常见问题

- **找不到 qmake**：把 Qt 的 `bin` 目录加入 PATH，或在 Qt Creator 里构建。  
- **影子构建目录很大**：正常，勿提交；确认 `.gitignore` 已生效。  
