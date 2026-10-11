# psDemo.pro — PhotoshopLite 主程序
#
# 源列表在 app.pri（与 tests/tests.pro 共用，避免两处维护漂移）。
# 常驻测试：cd tests && qmake && mingw32-make → psdemo_tests.exe
#
# 产物名 PSLite.exe（目录/仓库仍叫 psDemo/photoshopDemo，避免大范围改路径）

TARGET = PSLite

include(app.pri)

# main.cpp 只属于主程序（测试有各自的 testmain.cpp）
SOURCES += main.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
