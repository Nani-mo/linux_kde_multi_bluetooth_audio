# Backend-Logik als eigenständige statische Bibliothek für das Konsolen-
# Testtool (tools/) und die Unit-Tests (tests/), die keine QML-Engine
# brauchen. Siehe PLAN.md Abschnitt 3.
add_library(multibtaudiocore STATIC
    pipewirecontroller.h
    pipewirecontroller.cpp
    devicelistmodel.h
    devicelistmodel.cpp
    combinesinkmanager.h
    combinesinkmanager.cpp
    configstore.h
    configstore.cpp
)

target_include_directories(multibtaudiocore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(multibtaudiocore PUBLIC
    Qt6::Core
    Qt6::Qml
    KF6::I18n
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::CoreAddons
    PkgConfig::PIPEWIRE
)

# QML-Plugin-Modul für das Plasmoid-Frontend. Die Backend-Klassen sind hier
# ein zweites Mal als Quelle eingebunden (statt nur multibtaudiocore zu
# linken): QML_ELEMENT-Typregistrierung läuft über AUTOMOC/qmltyperegistrar
# und muss auf Sourcen desselben ecm_add_qml_module()-Targets laufen, sonst
# bleibt das generierte qmldir unvollständig (fehlendes typeinfo) und die
# QML-Engine findet das Modul zur Laufzeit nicht - mit plasmoidviewer
# verifiziert (siehe SETUP.md).
ecm_add_qml_module(multibtaudioplugin
    URI "org.kde.private.multibtaudio"
    VERSION 1.0
    GENERATE_PLUGIN_SOURCE
)

target_sources(multibtaudioplugin PRIVATE
    pipewirecontroller.h
    pipewirecontroller.cpp
    devicelistmodel.h
    devicelistmodel.cpp
    combinesinkmanager.h
    combinesinkmanager.cpp
    configstore.h
    configstore.cpp
)

target_link_libraries(multibtaudioplugin PRIVATE
    Qt6::Core
    Qt6::Qml
    Qt6::QmlIntegration
    Qt6::Quick
    KF6::I18n
    KF6::ConfigCore
    KF6::ConfigGui
    KF6::CoreAddons
    PkgConfig::PIPEWIRE
)

ecm_finalize_qml_module(multibtaudioplugin
    DESTINATION ${KDE_INSTALL_QMLDIR}
)
