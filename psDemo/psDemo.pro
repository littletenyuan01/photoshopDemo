QT += widgets

CONFIG += c++17

INCLUDEPATH += $$PWD

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    domain/layer.cpp \
    domain/layerstack.cpp \
    domain/imagedocument.cpp \
    engine/compositor.cpp \
    engine/paintengine.cpp \
    ui/canvasview.cpp \
    ui/canvasworkspace.cpp \
    ui/canvasdocstatusbar.cpp \
    ui/rulerwidget.cpp \
    ui/itemtreepanel.cpp \
    ui/layertreepanel.cpp \
    ui/channeltreepanel.cpp \
    ui/pathtreepanel.cpp \
    ui/layerpanel.cpp \
    ui/toolbox.cpp \
    ui/tooloptionsbar.cpp \
    ui/colorpickerdialog.cpp

HEADERS += \
    mainwindow.h \
    domain/blendmode.h \
    domain/layer.h \
    domain/layerstack.h \
    domain/imagedocument.h \
    engine/compositor.h \
    engine/paintengine.h \
    tools/toolid.h \
    ui/canvasview.h \
    ui/canvasworkspace.h \
    ui/canvasdocstatusbar.h \
    ui/rulerwidget.h \
    ui/itemtreepanel.h \
    ui/layertreepanel.h \
    ui/channeltreepanel.h \
    ui/pathtreepanel.h \
    ui/layerpanel.h \
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
