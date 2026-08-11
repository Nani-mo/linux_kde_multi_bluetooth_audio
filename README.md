# Plasma Multi-BT-Audio

KDE-Plasma-Systemtray-Applet zur simultanen Audio-Ausgabe über mehrere bereits verbundene Bluetooth-Geräte, auf Basis von PipeWire.

Siehe [PLAN.md](PLAN.md) für die vollständige Architektur- und Entwicklungsplanung.

## Status

Phasen 0–6 (Machbarkeits-Spike, Geräteerkennung, Multi-Output-Engine, Plasmoid-QML-UI, ConfigStore-Persistenz, Performance, Tests & Packaging) sind implementiert und auf dem CachyOS-Testsystem **Ende-zu-Ende mit echten Bluetooth-Geräten verifiziert**: JBL Flip 6 + Soundcore Liberty 4 Pro spielen hörbar synchron, die Geräteliste im Plasmoid-Popup zeigt beide live an, Checkbox-Auswahl und Lautstärke-Regler wirken beide real und werden pro Gerät gespeichert/automatisch wiederhergestellt, Verbindungsabbrüche werden automatisch abgefangen, der Ressourcen-Overhead ist nachweislich vernachlässigbar, und ein Arch-Paket lässt sich per `makepkg` bauen. Alle Unit-Tests laufen grün (siehe [SETUP.md](SETUP.md) für die vollständige Build-/Test-Historie inkl. mehrerer gefundener und behobener CMake/Qt6-Bugs). Offen: Delay-Regler ist noch reine Datenmodell-Ebene ohne echte Latenzkompensation, siehe [PLAN.md](PLAN.md) Abschnitt 4.

## Voraussetzungen

Entwicklung/Build erfordert eine Linux-Umgebung mit:

- KDE Plasma 6 / KDE Frameworks 6 (KF6)
- Qt 6 (Core, Qml, Quick)
- PipeWire Entwicklungspakete (`libpipewire-0.3-dev` bzw. distributionsabhängig)
- `extra-cmake-modules`
- CMake ≥ 3.16, ein C++20-fähiger Compiler

Auf CachyOS (Arch-basiert) z.B.:

```
sudo pacman -S extra-cmake-modules cmake qt6-base qt6-declarative kcoreaddons kconfig ki18n kpackage pipewire
```

## Bauen

```
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Installieren

### Mit CMake direkt

```
sudo cmake --install build
```

Installiert das QML-Plugin nach `$QT_INSTALL_QML` (wird automatisch korrekt ermittelt, siehe SETUP.md) und das Plasmoid-Package nach `/usr/share/plasma/plasmoids/com.nanimo.multibtaudio`.

### Als Arch-Paket

Ein [PKGBUILD](PKGBUILD) liegt im Projekt-Wurzelverzeichnis und wurde erfolgreich gegen das CachyOS-Testsystem gebaut (`makepkg`, siehe SETUP.md „Phase-6-Ergebnis"):

```
makepkg -si
```

Baut aktuell aus dem lokalen Arbeitsverzeichnis. Für eine echte Distribution über den GitLab-Server (siehe SETUP.md) müsste `PKGBUILD` auf eine Git-Quelle (`source=("git+<url>.git")`) umgestellt werden, sobald das Repository dort gepusht ist.

Danach das Plasmoid im Systemtray-Einstellungsdialog aktivieren, oder testweise ohne Panel-Änderung mit `plasmoidviewer -a plasmoid` (aus dem Paket `plasma-sdk`) vorschauen.

## Lizenz

GPL-3.0-or-later, siehe [LICENSE](LICENSE).
