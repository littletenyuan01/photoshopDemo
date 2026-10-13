# gpu_bench — GPU 收益端到端计时（走真实调度路径；一次性程序，不参与常驻用例）
#   qmake psDemo/gpu_bench.pro -o .build-verify/gpu_bench/build/Makefile
#   cd .build-verify/gpu_bench/build && mingw32-make -f Makefile.Release -j8
#   set PSLITE_RASTER=gpu && release/gpu_bench.exe
TARGET = gpu_bench
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app

include(app.pri)

SOURCES += tools/gpu_bench/main.cpp
