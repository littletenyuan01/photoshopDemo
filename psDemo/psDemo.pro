QT += widgets svg

CONFIG += c++17

INCLUDEPATH += $$PWD

# 分层：app（会话/广播）· domain（文档真相）· engine（算法）· tools（交互状态机）· ui（Qt 界面）
# 依赖方向强制单向：ui → app → tools/domain；engine 被 tools/domain 调用；
# domain/engine 禁止依赖 Qt Widgets。

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    app/appsession.cpp \
    domain/layer.cpp \
    domain/layerstack.cpp \
    domain/imagedocument.cpp \
    engine/compositor.cpp \
    engine/paintengine.cpp \
    tools/tool.cpp \
    tools/toolmanager.cpp \
    tools/movetool.cpp \
    tools/handtool.cpp \
    tools/zoomtool.cpp \
    tools/painttool.cpp \
    ui/canvasview.cpp \
    ui/canvasworkspace.cpp \
    ui/canvasdocstatusbar.cpp \
    ui/rulerwidget.cpp \
    ui/itemtreepanel.cpp \
    ui/layertreepanel.cpp \
    ui/channeltreepanel.cpp \
    ui/pathtreepanel.cpp \
    ui/dockpanel.cpp \
    ui/toolbox.cpp \
    ui/tooloptionsbar.cpp \
    ui/colorpickerdialog.cpp

HEADERS += \
    mainwindow.h \
    app/appsession.h \
    domain/blendmode.h \
    domain/layer.h \
    domain/layerstack.h \
    domain/imagedocument.h \
    engine/compositor.h \
    engine/paintengine.h \
    tools/toolid.h \
    tools/toolevent.h \
    tools/toolcontext.h \
    tools/tool.h \
    tools/toolmanager.h \
    tools/movetool.h \
    tools/handtool.h \
    tools/zoomtool.h \
    tools/painttool.h \
    ui/canvasview.h \
    ui/canvasworkspace.h \
    ui/canvasdocstatusbar.h \
    ui/rulerwidget.h \
    ui/itemtreepanel.h \
    ui/layertreepanel.h \
    ui/channeltreepanel.h \
    ui/pathtreepanel.h \
    ui/dockpanel.h \
    ui/toolbox.h \
    ui/tooloptionsbar.h \
    ui/colorpickerdialog.h

FORMS += \
    mainwindow.ui \
    ui/canvasworkspace.ui \
    ui/canvasdocstatusbar.ui \
    ui/layertreepanel.ui \
    ui/channeltreepanel.ui \
    ui/pathtreepanel.ui \
    ui/layerpanel.ui \
    ui/toolbox.ui \
    ui/tooloptionsbar.ui \
    ui/colorpickerdialog.ui

RESOURCES += \
    resources.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
