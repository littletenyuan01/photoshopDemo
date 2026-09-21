QT += widgets svg

CONFIG += c++17

INCLUDEPATH += $$PWD

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    domain/layer.cpp \
    domain/layerstack.cpp \
    domain/imagedocument.cpp \
    engine/compositor.cpp \
    ui/canvasview.cpp \
    ui/layerpanel.cpp \
    ui/toolbox.cpp \
    ui/tooloptionsbar.cpp

HEADERS += \
    mainwindow.h \
    domain/blendmode.h \
    domain/layer.h \
    domain/layerstack.h \
    domain/imagedocument.h \
    engine/compositor.h \
    tools/toolid.h \
    ui/canvasview.h \
    ui/layerpanel.h \
    ui/toolbox.h \
    ui/tooloptionsbar.h

FORMS += \
    mainwindow.ui \
    ui/layerpanel.ui \
    ui/toolbox.ui \
    ui/tooloptionsbar.ui

RESOURCES += \
    resources.qrc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
