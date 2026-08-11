cmake_minimum_required(VERSION 3.16)

project(plasma-multi-bt-audio
    VERSION 0.1.0
    DESCRIPTION "KDE Plasmoid for simultaneous audio output over multiple Bluetooth devices"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# multibtaudiocore (statische Lib) wird sowohl ins QML-Plugin (Shared Object)
# als auch ins Konsolentool (Executable) gelinkt - für Ersteres wird PIC
# benötigt, daher projektweit aktiviert statt nur pro Target.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Muss vor dem ersten find_package(Qt6 ...) gesetzt werden: Qt6QmlMacros.cmake
# bricht beim Einbinden sonst mit "qt_generate_foreign_qml_types() is only
# available in Qt 6" ab, weil diese Variable zu diesem Zeitpunkt noch nicht
# gesetzt ist (bekanntes Bootstrapping-Problem der Qt6-CMake-Module).
set(QT_DEFAULT_MAJOR_VERSION 6)

# ECMs eigene (von QT_DEFAULT_MAJOR_VERSION unabhängige!) Versionsvariable:
# QtVersionOption.cmake (von ECMQmlModule intern eingebunden) setzt
# QT_MAJOR_VERSION auf 5, sobald irgendwo im Konfigurationslauf ein
# Qt5::Core-Target existiert (auf diesem System zusätzlich zu Qt6
# installiert) - unabhängig davon, dass wir explizit Qt6 anfordern. Ohne
# diesen Fix landet ecm_add_qml_module() auf dem Qt5-Codepfad, der
# QML_ELEMENT/qmltyperegistrar nicht kennt: das generierte qmldir bleibt
# unvollständig (kein "typeinfo") und die QML-Engine findet das Modul zur
# Laufzeit nicht ("module ... is not installed", mit plasmoidviewer über
# mehrere Anläufe verifiziert - siehe SETUP.md).
set(QT_MAJOR_VERSION 6)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

# Muss nach find_package(Qt6 ... Qml ...) eingebunden werden: ECMQmlModule
# sucht selbst nach Qt6Qml, und dessen CMake-Makros benötigen
# QT_DEFAULT_MAJOR_VERSION, das erst durch Qt6Core gesetzt wird. Bei
# umgekehrter Reihenfolge bricht die Konfiguration mit
# "qt_generate_foreign_qml_types() is only available in Qt 6" ab, obwohl
# Qt 6 korrekt gefunden wird.
find_package(Qt6 REQUIRED COMPONENTS Core Qml Quick QmlIntegration)
include(ECMQmlModule)

# KDEInstallDirs errät KDE_INSTALL_QMLDIR teils falsch (auf diesem
# Arch/CachyOS-Testsystem: "lib/qt/qml" statt dem tatsächlichen
# "lib/qt6/qml") - ein dorthin installiertes Plugin findet die QML-Engine
# dann nicht ("module ... is not installed", mit plasmoidviewer
# verifiziert). Daher den echten Pfad direkt von qmake6 abfragen.
find_program(QMAKE6_EXECUTABLE NAMES qmake6 qmake)
if(QMAKE6_EXECUTABLE)
    execute_process(
        COMMAND ${QMAKE6_EXECUTABLE} -query QT_INSTALL_QML
        OUTPUT_VARIABLE QT_ACTUAL_QML_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(QT_ACTUAL_QML_DIR)
        set(KDE_INSTALL_QMLDIR "${QT_ACTUAL_QML_DIR}" CACHE PATH "Qt6 QML modules directory" FORCE)
    endif()
endif()

find_package(KF6 REQUIRED COMPONENTS I18n Config CoreAddons Package)

find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)

# Mindestversion noch nicht final validiert (siehe PLAN.md, Phase 0 Spike:
# module-combine-stream mit latency-compensate braucht eine hinreichend
# aktuelle PipeWire-Version). Wert hier vorläufig, auf CachyOS-Testsystem
# gegen tatsächlich installierte Version prüfen.
if(PIPEWIRE_VERSION VERSION_LESS "0.3.65")
    message(WARNING "libpipewire ${PIPEWIRE_VERSION} gefunden - Mindestversion für module-combine-stream noch nicht final verifiziert (siehe PLAN.md Phase 0).")
endif()

add_subdirectory(src)

option(BUILD_TOOLS "Build the btaudio-debug console tool" ON)
if(BUILD_TOOLS)
    add_subdirectory(tools)
endif()

option(BUILD_TESTING_BTAUDIO "Build unit tests" OFF)
if(BUILD_TESTING_BTAUDIO)
    enable_testing()
    add_subdirectory(tests)
endif()

install(DIRECTORY plasmoid/
    DESTINATION ${KDE_INSTALL_DATADIR}/plasma/plasmoids/com.nanimo.multibtaudio
)
