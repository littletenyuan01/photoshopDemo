# 一次性端到端计时（gpu-plan T1.14：源码在树内，干净 clone 可构建）。
#   qmake psDemo/median_e2e.pro -o .build-verify/median_e2e/build/Makefile
#   cd .build-verify/median_e2e/build && mingw32-make -f Makefile.Release -j8
#   release/median_e2e.exe
TARGET = median_e2e
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app

include(app.pri)

SOURCES += tools/median_e2e/main.cpp
