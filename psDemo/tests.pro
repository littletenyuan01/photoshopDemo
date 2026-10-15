# tests.pro — 常驻测试工程（无 GUI 断言，不需要人工看画面）
#
# 【为什么本 .pro 放在 psDemo/ 而不是 tests/】
# qmake 里 .pri 中的**相对路径按「包含它的 .pro 所在目录」解析**（不是 .pri 自己的目录），
# 所以 app.pri 里的 `domain/layer.cpp` 这类路径只有在 .pro 与 app.pri 同目录时才成立。
# 测试**源文件**仍在 tests/ 子目录，只是工程文件放在这里。
#
# 构建与运行：
#   mkdir build && cd build
#   qmake <repo>/psDemo/tests.pro && mingw32-make -j4
#   release/psdemo_tests.exe                  → 跑全部用例
#   release/psdemo_tests.exe composite regionraster  → 只跑指定用例
#
# 退出码：0 = 全部通过；非 0 = 失败用例数（便于脚本 / CI 判定）。

TARGET = psdemo_tests
CONFIG += console
CONFIG -= app_bundle

include(app.pri)

SOURCES += \
    tests/testmain.cpp \
    tests/t_pixelprim.cpp \
    tests/t_filterchain.cpp \
    tests/t_regionraster.cpp \
    tests/t_composite.cpp \
    tests/t_graphcheck.cpp \
    tests/t_projectio.cpp \
    tests/t_adjustpanel.cpp \
    tests/t_adjustapply.cpp \
    tests/t_blendparallel.cpp \
    tests/t_transform.cpp \
    tests/t_dirtybudget.cpp \
    tests/t_areafilter.cpp \
    tests/t_filterundo.cpp \
    tests/t_filterpanel.cpp \
    tests/t_filterasync.cpp \
    tests/t_roi.cpp \
    tests/t_offset.cpp \
    tests/t_morefilters.cpp \
    tests/t_hpblur.cpp \
    tests/t_noise.cpp \
    tests/t_batcha.cpp \
    tests/t_ripple.cpp \
    tests/t_rasterbackend.cpp \
    tests/t_gpumedian.cpp \
    tests/t_gpucustom.cpp \
    tests/t_gput47.cpp \
    tests/t_gpukernels.cpp \
    tests/t_adjarea.cpp \
    tests/t_c1pixelate.cpp \
    tests/t_c1stylize.cpp \
    tests/t_webexport.cpp \
    tests/t_psdio.cpp \
    tests/t_pathgeom.cpp \
    tests/t_pathraster.cpp \
    tests/t_pentool.cpp \
    tests/t_pathtree.cpp \
    tests/t_tilecache.cpp \
    tests/t_phase1a.cpp \
    tests/t_projectionvalid.cpp \
    tests/t_undocow.cpp \
    tests/t_undotile.cpp \
    tests/t_undoregion.cpp \
    tests/t_undodirty.cpp \
    tests/t_undobatch.cpp

HEADERS += \
    tests/testutil.h
