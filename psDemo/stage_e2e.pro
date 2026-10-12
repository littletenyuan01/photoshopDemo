# T4.2 三段计时（需 PSLITE_GPU_STAGE，由 main 自行设置）
#   qmake psDemo/stage_e2e.pro -o .build-verify/stage_e2e/build/Makefile
TARGET = stage_e2e
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app

include(app.pri)

SOURCES += tools/stage_e2e/main.cpp
