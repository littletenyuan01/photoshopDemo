# 自定滤镜 CPU ns/px + GPU 端到端（gpu-plan T2.0 / T2.5）
#   qmake psDemo/custom_e2e.pro -o .build-verify/custom_e2e/build/Makefile
#   cd .build-verify/custom_e2e/build && mingw32-make -f Makefile.Release -j8
TARGET = custom_e2e
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app

include(app.pri)

SOURCES += tools/custom_e2e/main.cpp
