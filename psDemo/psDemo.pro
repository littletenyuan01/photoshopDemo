QT += widgets svg

CONFIG += c++17

# 产物名 PSLite.exe（目录/仓库仍叫 psDemo/photoshopDemo，避免大范围改路径）
TARGET = PSLite

INCLUDEPATH += $$PWD

# 分层：app（会话/广播）· domain（文档真相）· engine（算法）· tools（交互状态机）· ui（Qt 界面）
# 依赖方向强制单向：ui → app → tools/domain；engine 被 tools/domain 调用；
# domain/engine 禁止依赖 Qt Widgets。

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    app/appsession.cpp \
    app/recentdocuments.cpp \
    app/historystack.cpp \
    app/undoitem.cpp \
    domain/layer.cpp \
    domain/tilebuffer.cpp \
    domain/layerstack.cpp \
    domain/selection.cpp \
    domain/filterstack.cpp \
    domain/imagedocument.cpp \
    io/projectio.cpp \
    io/psdio.cpp \
    io/rasterio.cpp \
    engine/blend.cpp \
    engine/compositor.cpp \
    engine/projection.cpp \
    engine/paintengine.cpp \
    engine/filtereval.cpp \
    engine/op/layermodeop.cpp \
    engine/op/floodfillop.cpp \
    engine/op/gradientop.cpp \
    engine/op/stampdabop.cpp \
    engine/op/clonestampdabop.cpp \
    engine/op/focusdabop.cpp \
    engine/op/tonedabop.cpp \
    engine/op/shapefillop.cpp \
    engine/op/solidfillop.cpp \
    engine/op/selectpolygonop.cpp \
    engine/op/selectfloodop.cpp \
    engine/op/opregistry.cpp \
    engine/op/oprunner.cpp \
    engine/op/opsinit.cpp \
    engine/op/opname.cpp \
    engine/op/pointopregistry.cpp \
    tools/tool.cpp \
    tools/toolmanager.cpp \
    tools/movetool.cpp \
    tools/handtool.cpp \
    tools/zoomtool.cpp \
    tools/painttool.cpp \
    tools/paintbuckettool.cpp \
    tools/gradienttool.cpp \
    tools/marqueeselecttool.cpp \
    tools/lassotool.cpp \
    tools/polygonallassotool.cpp \
    tools/magneticlassotool.cpp \
    tools/magicwandtool.cpp \
    tools/quickselecttool.cpp \
    tools/croptool.cpp \
    tools/eyedroppertool.cpp \
    tools/clonestamptool.cpp \
    tools/focustool.cpp \
    tools/tonetool.cpp \
    tools/shapetool.cpp \
    ui/canvasview.cpp \
    ui/canvasworkspace.cpp \
    ui/canvasdocstatusbar.cpp \
    ui/rulerwidget.cpp \
    ui/itemtreepanel.cpp \
    ui/layertreepanel.cpp \
    ui/channeltreepanel.cpp \
    ui/pathtreepanel.cpp \
    ui/dockpanel.cpp \
    ui/colorspanel.cpp \
    ui/infopanel.cpp \
    ui/hsvcolorwell.cpp \
    ui/labeledlineedit.cpp \
    ui/labeledcombobox.cpp \
    ui/propertiespanel.cpp \
    ui/toolbox.cpp \
    ui/tooloptionsbar.cpp \
    ui/colorpickerdialog.cpp \
    ui/homescreen.cpp \
    ui/newdocumentdialog.cpp \
    ui/imagesizedialog.cpp \
    ui/canvassizedialog.cpp

HEADERS += \
    mainwindow.h \
    app/appsession.h \
    app/recentdocuments.h \
    app/historystack.h \
    app/undoitem.h \
    domain/blendmode.h \
    domain/layer.h \
    domain/tilebuffer.h \
    domain/layerstack.h \
    domain/selection.h \
    domain/filternode.h \
    domain/filterstack.h \
    domain/imagedocument.h \
    io/projectio.h \
    io/psdio.h \
    io/rasterio.h \
    engine/blend.h \
    engine/compositor.h \
    engine/projection.h \
    engine/paintengine.h \
    engine/filtereval.h \
    engine/paintselectionclip.h \
    engine/painttypes.h \
    engine/premul.h \
    engine/magneticedgesnap.h \
    engine/op/operation.h \
    engine/op/pointop.h \
    engine/op/bufferop.h \
    engine/op/opcontext.h \
    engine/op/paintclip.h \
    engine/op/layermodeop.h \
    engine/op/floodfillop.h \
    engine/op/gradientop.h \
    engine/op/stampdabop.h \
    engine/op/clonestampdabop.h \
    engine/op/focusdabop.h \
    engine/op/tonedabop.h \
    engine/op/shapefillop.h \
    engine/op/solidfillop.h \
    engine/op/selectpolygonop.h \
    engine/op/selectfloodop.h \
    engine/op/oppad.h \
    engine/op/opregistry.h \
    engine/op/oprunner.h \
    engine/op/opsinit.h \
    engine/op/opname.h \
    engine/op/pointopregistry.h \
    engine/op/layermodecatalog.h \
    tools/toolid.h \
    tools/toolevent.h \
    tools/toolcontext.h \
    tools/tool.h \
    tools/toolmanager.h \
    tools/movetool.h \
    tools/handtool.h \
    tools/zoomtool.h \
    tools/painttool.h \
    tools/paintbuckettool.h \
    tools/gradienttool.h \
    tools/marqueeselecttool.h \
    tools/lassotool.h \
    tools/polygonallassotool.h \
    tools/magneticlassotool.h \
    tools/magicwandtool.h \
    tools/quickselecttool.h \
    tools/croptool.h \
    tools/eyedroppertool.h \
    tools/clonestamptool.h \
    tools/focustool.h \
    tools/tonetool.h \
    tools/shapetool.h \
    ui/canvasview.h \
    ui/canvasworkspace.h \
    ui/canvasdocstatusbar.h \
    ui/rulerwidget.h \
    ui/itemtreepanel.h \
    ui/layertreepanel.h \
    ui/channeltreepanel.h \
    ui/pathtreepanel.h \
    ui/dockpanel.h \
    ui/pixmaputils.h \
    ui/colorspanel.h \
    ui/infopanel.h \
    ui/hsvcolorwell.h \
    ui/labeledlineedit.h \
    ui/labeledcombobox.h \
    ui/propertiespanel.h \
    ui/toolbox.h \
    ui/tooloptionsbar.h \
    ui/colorpickerdialog.h \
    ui/homescreen.h \
    ui/newdocumentdialog.h \
    ui/imagesizedialog.h \
    ui/canvassizedialog.h

FORMS += \
    mainwindow.ui \
    ui/canvasworkspace.ui \
    ui/canvasdocstatusbar.ui \
    ui/layertreepanel.ui \
    ui/channeltreepanel.ui \
    ui/pathtreepanel.ui \
    ui/layerpanel.ui \
    ui/colorspanel.ui \
    ui/infopanel.ui \
    ui/propertiespanel.ui \
    ui/toolbox.ui \
    ui/tooloptionsbar.ui \
    ui/colorpickerdialog.ui \
    ui/homescreen.ui \
    ui/newdocumentdialog.ui \
    ui/imagesizedialog.ui \
    ui/canvassizedialog.ui

RESOURCES += \
    resources.qrc

# Windows：把 ICO 嵌进 exe，任务栏 / 资源管理器才显示应用图标
# （仅 setWindowIcon 往往只影响标题栏小图标，任务栏仍用 exe 默认窗体图标）
win32: RC_ICONS = resources/icons/ui/app-logo.ico

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
