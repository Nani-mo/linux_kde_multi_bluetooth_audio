# Multi-Bluetooth-Audio-Output für KDE Plasma — Projektplan

## 1. Ziel

Eine performante, ressourcenschonende KDE-Plasma-Anwendung, die es erlaubt:

- mehrere bereits gekoppelte/verbundene Bluetooth-Audiogeräte gleichzeitig als Wiedergabeziel zu nutzen (simultane, kombinierte Ausgabe — kein reines Umschalten),
- die Geräteauswahl bequem über ein Icon im KDE-Systemtray zu steuern,
- auf PipeWire als Audio-Backend aufzusetzen (kein PulseAudio-Fallback nötig, da moderne KDE-Systeme PipeWire nutzen),
- **kein** eigenes Bluetooth-Pairing zu übernehmen — Kopplung/Verbindung bleibt Aufgabe der System-Bluetooth-Einstellungen (`bluedevil`/`kdeconnect` sind hier nicht involviert, nur die BlueZ-Verbindungen, die bereits bestehen).

Nicht-Ziele (Scope-Abgrenzung): Pairing-UI, Mikrofon-/Input-Routing, Multi-Room-Sync über Netzwerk, Windows/macOS-Support.

## 2. Architektur-Überblick

Die Anwendung wird als **Plasmoid mit C++-Backend-Plugin** gebaut — das ist die native Form eines Systemtray-Eintrags in Plasma 6 (analog zum Lautstärke-Applet) und zugleich die ressourcenschonendste Variante, da kein eigener Fensterprozess samt Event-Loop läuft, sondern der Code im `plasmashell`-Prozess bzw. im systray-Hostprozess lebt.

```
┌─────────────────────────────────────────────────────────┐
│ Plasma Systemtray (plasmashell)                          │
│                                                           │
│  ┌─────────────────────────────────────────────────┐    │
│  │ Plasmoid "Multi-BT-Audio"                        │    │
│  │                                                   │    │
│  │  QML Frontend (Kirigami)                         │    │
│  │   - Geräteliste mit Checkboxen                   │    │
│  │   - Pro-Gerät Lautstärke/Delay-Slider            │    │
│  │   - Tray-Icon-Status                             │    │
│  │        │  Qt Property Bindings / Signals         │    │
│  │        ▼                                         │    │
│  │  C++ Backend-Plugin (QQmlEngine Extension)       │    │
│  │   - DeviceListModel (QAbstractListModel)         │    │
│  │   - PipeWireController                           │    │
│  │   - CombineSinkManager                           │    │
│  │   - ConfigStore (KConfig)                        │    │
│  └──────────────────┬────────────────────────────────┘  │
└─────────────────────┼──────────────────────────────────┘
                       │ libpipewire (C API), direkte Verbindung
                       ▼
┌─────────────────────────────────────────────────────────┐
│ PipeWire Daemon + WirePlumber (Session Manager)          │
│  - BT-Sinks (media.class=Audio/Sink, device.api=bluez5)  │
│  - module-combine-stream → virtueller "Multi-Out"-Sink   │
└─────────────────────────────────────────────────────────┘
```

**Warum kein eigener Daemon-Prozess?** Ein separater Hintergrunddienst wäre eine zusätzliche laufende Instanz mit eigenem Speicher-Footprint und IPC-Overhead zur GUI. Da das Plasmoid ohnehin dauerhaft im systray-Prozess lebt, kann die PipeWire-Verbindung direkt dort gehalten werden (ein `pw_thread_loop`, ereignisbasiert, keine Polling-Schleifen). Das spart einen kompletten Prozess samt D-Bus-Kommunikation.

## 3. Kernkomponenten

### 3.1 PipeWireController (C++)
- Hält eine persistente Verbindung zum PipeWire-Daemon (`pw_context` + `pw_thread_loop`, eigener Thread, damit die UI nie blockiert).
- Abonniert die Registry (`pw_registry_add_listener`) und filtert Nodes mit `media.class=Audio/Sink` und `device.api=bluez5` (bzw. prüft `device.bus`/`api.bluez5.address`) — das sind die bereits verbundenen BT-Ausgabegeräte, die WirePlumber automatisch anlegt.
- Emittiert Qt-Signale bei Geräte-Hinzufügen/-Entfernen/-Umbenennen (z.B. Verbindungsabbruch eines BT-Geräts während der Nutzung).

### 3.2 DeviceListModel (C++, QAbstractListModel)
- Bindet die von `PipeWireController` erkannten Geräte an die QML-Liste.
- Felder pro Gerät: Name, Icon (Kopfhörer/Lautsprecher via `device.form-factor`), Verbindungsstatus, „aktiv in Kombi-Ausgabe" (bool), Lautstärke, Delay-Offset (ms).

### 3.3 CombineSinkManager (C++) — Kernstück
Verantwortlich für das eigentliche Multi-Output-Routing. Zwei mögliche technische Wege, beide über die PipeWire-Modul-Infrastruktur:

**Primärer Ansatz — `libpipewire-module-combine-stream`:**
PipeWire bringt seit Version 0.3.6x ein natives Modul mit, das genau diesen Zweck erfüllt: einen virtuellen Sink erzeugen, der eingehendes Audio auf mehrere Ziel-Sinks verteilt, inklusive `latency-compensate`-Option zum Ausgleich unterschiedlicher Geräte-Latenzen. Das Modul wird dynamisch nachgeladen (`pw_core_load_module()` über die C-API oder als Fallback per `pw-cli load-module ...`-Subprozessaufruf, falls die direkte API sich in der Praxis als zu instabil erweist).

Beim Ändern der Geräteauswahl im UI wird das Modul mit neuer `targets`-Liste entladen und neu geladen (bzw. bei API-Unterstützung: Ziel-Liste live aktualisiert).

**Fallback-Ansatz — manuelle Loopback-Verkettung:**
Falls sich `combine-stream` in der Praxis als zu unflexibel/instabil herausstellt (dies muss in Phase 0 verifiziert werden, siehe unten):
1. Ein virtueller Null-Sink wird als Systemausgabeziel angelegt ("Multi-BT-Output").
2. Für jedes ausgewählte BT-Gerät wird ein `libpipewire-module-loopback` erzeugt, das vom virtuellen Sink kapturiert und auf den jeweiligen BT-Sink wiedergibt (`target.object=<bt-sink-serial>`).
3. Vorteil: pro Gerät granular steuerbar (eigene Lautstärke/Delay pro Loopback-Instanz über `stream.props`), Nachteil: mehr laufende Streams/Overhead als eine einzelne combine-stream-Instanz.

Die Architektur kapselt beide hinter derselben `CombineSinkManager`-Schnittstelle, sodass der gewählte Mechanismus austauschbar bleibt.

### 3.4 QML-Frontend (Kirigami)
- Kompaktes Popup analog zum Lautstärke-Plasmoid: Liste der verbundenen BT-Audiogeräte, Checkbox „in Kombi-Ausgabe einschließen", Mini-Slider für Lautstärke, optionaler Expander für Delay-Feinabstimmung (siehe Latenzproblem unten).
- Tray-Icon-Status: zeigt visuell an, ob 0/1/mehrere Geräte aktiv sind (z.B. unterschiedliche Icon-Badges).

### 3.5 ConfigStore (KConfig)
- Speichert zuletzt genutzte Geräte-Kombination (per Geräte-MAC identifiziert) und Lautstärke-/Delay-Werte, damit sie bei erneutem Verbinden automatisch vorgeschlagen/wiederhergestellt werden können.

## 4. Technischer Deep-Dive: Latenz & Synchronität

Das ist die größte technische Risikofläche des Projekts und sollte früh (Phase 0) geklärt werden:

- Bluetooth-A2DP-Codecs (SBC, AAC, aptX, aptX-LL, LDAC …) haben stark unterschiedliche inhärente Latenzen (SBC: oft 150–300 ms, aptX-LL: ~40 ms). Werden zwei Geräte mit unterschiedlichen Codecs kombiniert, driftet die Wiedergabe hörbar auseinander (Phasenversatz, Echo-Effekt), wenn nicht kompensiert wird.
- `module-combine-stream` bietet `latency-compensate`, das die schnelleren Geräte künstlich verzögert, bis sie dem langsamsten Gerät entsprechen — das gleicht die *initiale* Latenz aus, aber nicht Uhren-Drift über Zeit (Bluetooth-Takt vs. PipeWire-Graph-Takt laufen nie perfekt synchron).
- **Mitigation im Scope dieses Projekts:**
  - Anzeige der (aus PipeWire auslesbaren) `node.latency`/`api.bluez5.codec` pro Gerät in der UI, damit der Nutzer weiß, welche Kombination unproblematisch ist.
  - Manueller Delay-Offset-Slider pro Gerät als Nutzer-Werkzeug (kein Autokalibrierungs-Feature in v1 — das wäre ein eigenes Projekt mit Messsignal/Mikrofon-Rückkopplung).
  - Dokumentierter Hinweis: beste Ergebnisse mit gleichartigen Geräten/Codecs; harte Sample-genaue Sync über heterogene BT-Geräte ist physikalisch/protokollbedingt nicht exakt erreichbar.

## 5. Tech-Stack

| Bereich | Wahl | Begründung |
|---|---|---|
| Sprache | C++20 | Performance, native KF6-Integration |
| UI | QML + Kirigami (KF6) | Plasma-6-Standard für Plasmoids |
| Audio-Backend | libpipewire (C-API, direkt gelinkt) | kein Subprozess-Overhead, ereignisbasiert |
| BT-Geräteerkennung | über PipeWire-Node-Metadaten (`device.api=bluez5`) | kein eigener BlueZ-D-Bus-Code nötig, da PipeWire/WirePlumber das schon aufbereitet |
| Konfiguration | KConfig (KF6) | Plasma-Standard, XDG-konform |
| Build | CMake + `extra-cmake-modules` | Standard für KDE-Software |
| Paketierung | Plasmoid-Metadata (`metadata.json`) + `.so`-Plugin via `kpackagetool6` | native Distribution, keine Flatpak-Sandbox-Probleme mit PipeWire-Socket |

Kein Python, kein Qt Widgets, keine zusätzliche IPC-Schicht (D-Bus) für die Kernfunktion — das hält den Ressourcenverbrauch minimal.

## 6. Projektstruktur

```
linux_kde_bluetooth_sharing/
├── CMakeLists.txt
├── plasmoid/
│   ├── metadata.json
│   ├── contents/
│   │   ├── ui/
│   │   │   ├── main.qml
│   │   │   ├── DeviceItem.qml
│   │   │   └── CompactRepresentation.qml
│   │   └── config/
├── src/
│   ├── pipewirecontroller.{h,cpp}
│   ├── devicelistmodel.{h,cpp}
│   ├── combinesinkmanager.{h,cpp}
│   ├── configstore.{h,cpp}
│   └── plugin.{h,cpp}      # QQmlEngineExtensionPlugin Registrierung
├── tests/
│   ├── test_devicelistmodel.cpp
│   └── test_combinesinkmanager.cpp   # gegen echten pipewire-Test-Daemon
└── PLAN.md
```

## 7. Entwicklungsphasen

### Phase 0 — Spike / Machbarkeitsprüfung (kritisch, zuerst) — ✅ abgeschlossen
- [x] `pw-cli`/`wpctl` manuell nutzen, um zu verifizieren, dass `module-combine-stream` sich zur Laufzeit laden/entladen lässt und tatsächlich synchron auf zwei BT-Sinks ausgibt. **Erfolgreich mit echten Geräten getestet** (JBL Flip 6 + Soundcore Liberty 4 Pro, siehe SETUP.md „Phase-0-Spike-Ergebnis") — Nutzer hat den Testton hörbar auf beiden Geräten gleichzeitig bestätigt.
- [x] Verifizieren, welche PipeWire-Version auf Zielsystemen verfügbar ist — auf dem CachyOS-Testlaptop: **1.6.8** (siehe SETUP.md), deutlich über der vorläufigen Mindestversion 0.3.65. Weitere Zielsysteme/Distros noch offen.
- [x] Testen, ob `pw_core_load_module()` clientseitig ohne erhöhte Rechte funktioniert. **Ja** — Laden über einen normalen `pw-cli`-Client (keine Root-/erhöhten Rechte) hat funktioniert, kein Fallback nötig.
- **Ergebnis: Ansatz „combine-stream" (Abschnitt 3.3, Primärer Ansatz) ist bestätigt und wird für Phase 2 umgesetzt.** Wichtige Erkenntnis: Der vom Client geladene Sink lebt nur so lange wie dessen PipeWire-Verbindung (`object.register=false`, nicht in der normalen Geräteliste sichtbar) — passt zur geplanten Architektur (Verbindung lebt dauerhaft im systray-Hostprozess, siehe Abschnitt 2).

### Phase 1 — Core Backend: Geräteerkennung
- [x] `PipeWireController`: Verbindung, Registry-Listener, Filterung auf BT-Sinks. Auf dem CachyOS-Testsystem gebaut und mit zwei echten Geräten (JBL Flip 6, Soundcore Liberty 4 Pro) verifiziert — siehe SETUP.md für einen dabei gefundenen und behobenen Bug (BT-Properties erst nach Node-Bind verfügbar, nicht im Registry-global-Event).
- [x] `DeviceListModel` inkl. Unit-Tests (Mock der PipeWire-Events). Unit-Tests laufen grün auf dem Testsystem.
- [x] Konsolen-Testtool (`btaudio-debug`), das erkannte Geräte loggt — vor jeder UI-Arbeit die Backend-Logik isoliert verifizieren. Erkennt beide Testgeräte korrekt inkl. Codec (AAC/SBC) und BT-Adresse.

### Phase 2 — Multi-Output-Engine
- [x] `CombineSinkManager` implementiert: lädt `libpipewire-module-combine-stream` per `pw_context_load_module()` in den lokalen Client-Context von `PipeWireController` (siehe SETUP.md „Phase-0-Spike-Ergebnis" für die Herleitung dieser API). **End-to-End mit echter Hardware verifiziert** (JBL Flip 6 + Soundcore Liberty 4 Pro, hörbar synchron über den vom eigenen Code — nicht mehr manuell per `pw-cli` — erzeugten Sink). Setzt den erzeugten Sink zusätzlich per `wpctl set-default` als System-Standardausgabe (siehe SETUP.md „Kombi-Sink wird nicht zur Standard-Ausgabe") - ohne das spielten normale Anwendungen (mit Spotify verifiziert) weiterhin nur über das bisherige Gerät ab, obwohl der Kombi-Sink technisch korrekt lief.
- [~] Dynamisches Hinzufügen/Entfernen einzelner Ziele: aktuell über vollständiges Entladen+Neuladen des Moduls mit der kompletten neuen Zielliste gelöst (`setActiveTargets()` ist idempotent, jederzeit mit neuer Auswahl aufrufbar) — das verursacht eine kurze Unterbrechung auch für bereits laufende Geräte. Ob `stream.rules` sich unterbrechungsfrei live nachschieben lassen (ohne Reload), ist noch nicht untersucht.
- [x] Behandlung von Verbindungsabbrüchen: automatisch verkabelt über `DeviceListModel::onSinkRemoved` → `updateCombineTargets()` (Phase 3) — **mit echter Hardware verifiziert** (siehe SETUP.md „Phase-6-Ergebnis": `bluetoothctl disconnect` während aktiver Kombi-Ausgabe, verbleibendes Gerät spielt unterbrechungsfrei weiter).

### Phase 3 — Plasmoid-UI — ✅ Kernfunktion abgeschlossen
- [x] QML-Popup mit Geräteliste, Checkboxen, Lautstärke-Sliders. **Mit `plasmoidviewer` und echten Geräten visuell verifiziert** (siehe SETUP.md „Phase-3-Ergebnis"). Checkbox-Auswahl steuert direkt `CombineSinkManager` über `DeviceListModel` (automatisches Durchreichen der aktiven Auswahl, inkl. Verbindungsabbruch-Fall aus Phase 2). Lautstärke-Slider wirken jetzt echt (`PipeWireController::setNodeVolume()` über `wpctl set-volume`, siehe SETUP.md „Lautstärke-Ergebnis" für die Begründung, warum direktes `pw_node_set_param()` nicht funktioniert hat) - mit echter Hardware verifiziert.
- [x] Compact-Representation (Tray-Icon) mit Statusanzeige (0/1/mehrere aktive Geräte über `DeviceListModel::activeCount`, Icon- und Badge-Wechsel). **Im echten Panel getestet** - dabei einen Klick-öffnet-kein-Popup-Bug gefunden und behoben (`plasmoidItem.expanded` statt des wirkungslosen globalen `Plasmoid.expanded`, siehe SETUP.md „Klick-öffnet-kein-Popup-Bug"). `plasmoidviewer` hatte das nicht aufgedeckt, da es standardmäßig direkt die volle Ansicht zeigt, nie den Klick-Pfad über die Compact-Repräsentation durchläuft.
- [x] Delay-Offset-UI (Expander mit SpinBox, schreibt in `DelayMsRole`) - reine UI-/Datenmodell-Ebene. Ein direkter Ansatz über `SPA_PROP_latencyOffsetNsec` auf dem BT-Sink-Node wurde implementiert und getestet, hatte im Hörtest mit echten Geräten aber **keine Wirkung** und wurde wieder entfernt (siehe SETUP.md „Delay-Ergebnis"). Echte Audio-Latenzkompensation im Backend bleibt offen (Abschnitt 4) - vermutlich müsste der Ansatz über die `stream.rules`/`create-stream`-Properties beim Laden von `module-combine-stream` erfolgen statt über eine nachträgliche externe Property auf dem Sink.

### Phase 4 — Komfortfeatures & Persistenz
- [x] `ConfigStore`: letzte Geräteauswahl/-lautstärke pro Geräte-MAC speichern und automatisch vorschlagen. **Mit echter Hardware verifiziert:** `DeviceListModel` persistiert Active/Volume/DelayMs bei jeder Änderung (`~/.config/plasma-multi-bt-audiorc`, Gruppe pro BT-MAC) und wendet gespeicherte Werte beim Wiedersehen desselben Geräts automatisch an - inkl. automatischer Reaktivierung der Kombi-Ausgabe für zuvor aktive Geräte.
- [ ] Optional: Profile (z.B. „Büro" = Kopfhörer, „Wohnzimmer" = zwei Lautsprecher).

### Phase 5 — Performance-Tuning — ✅ abgeschlossen
- [x] Speicher-/CPU-Messung im Leerlauf und unter Last: **~34 MB RSS, exakt 0 zusätzliche CPU-Ticks über 60s Leerlauf; nur 30ms CPU-Zeit über 13s aktiver Zwei-Geräte-Wiedergabe** (siehe SETUP.md „Phase-5-Ergebnis"). Gemessen am Backend-Prozess (`btaudio-debug`) mangels installierter `heaptrack`/`perf`-Tools über `/proc/<pid>/stat`. Zusätzlich das Plasmoid testweise (mit Zustimmung) live ins echte Panel eingefügt und `plasmashell`-RSS verglichen: kein messbarer Unterschied (Änderung lag innerhalb der normalen Schwankungsbreite des Shell-Prozesses) - danach sauber wieder entfernt, Original-Panel-Zustand verifiziert.
- [x] Sicherstellen, dass keine Polling-Loops existieren: **projektweiter Code-Review bestätigt** - kein `QTimer`, kein `sleep`, keine Endlosschleifen, kein QML-`Timer{}` im gesamten Code (siehe SETUP.md).

### Phase 6 — Tests & Packaging
- [x] Manuelle Testmatrix: Verbindungsabbruch-Szenario und Neustart mit aktiver Kombi-Ausgabe **mit echter Hardware verifiziert** (siehe SETUP.md „Phase-6-Ergebnis"). 2 Geräte unterschiedlicher Codecs (AAC/SBC) getestet - nur 2 physische Testgeräte vorhanden, 3-4-Geräte-Szenario nicht real testbar, aber architektonisch nicht anders (generische Zielliste).
- [x] `metadata.json` (steht seit Phase 1) + `PKGBUILD` für Arch/CachyOS im Projekt-Wurzelverzeichnis, **erfolgreich mit `makepkg` gegen das Testsystem gebaut** (siehe SETUP.md „Phase-6-Ergebnis" und README.md „Installieren"). Baut aktuell aus dem lokalen Arbeitsverzeichnis statt von einer Git-Quelle - Umstellung auf `source=("git+...")` folgt, sobald das Repo auf dem GitLab-Server gepusht ist. `.deb`/RPM-Spec nicht umgesetzt (kein Zielsystem dafür vorhanden, „ggf." in der ursprünglichen Planung).

## 8. Risiken & offene Punkte

| Risiko | Auswirkung | Umgang |
|---|---|---|
| `pw_core_load_module` clientseitig eingeschränkt/verboten | Primärer Ansatz nicht direkt umsetzbar | Fallback: WirePlumber-Lua-Konfigurationsdatei dynamisch schreiben + WirePlumber-Reload, oder Subprozess `pw-cli` |
| Audio-Drift zwischen Geräten unterschiedlicher Codecs | Hörbare Sync-Probleme | Nutzeraufklärung + manueller Delay-Slider (siehe Abschnitt 4), kein Vollautomatik-Fix in v1 |
| Plasma-Systray-API-Änderungen zwischen Plasma-Minor-Versionen | Breaking Changes bei Plasma-Updates | An aktueller Plasma-6-LTS-Version entwickeln, Systray-Spezifikation (`org.kde.plasma.systemtray`) einhalten |
| WirePlumber-Konfiguration variiert je Distribution | Node-Metadaten-Namen (`device.api`, `api.bluez5.*`) könnten leicht abweichen | Robuste Filterung mit Fallback-Heuristiken, gegen mehrere Distros testen (Kubuntu, Arch, Fedora KDE) |

## 9. Nächste Schritte

1. Phase 0 (Spike) durchführen — das entscheidet die konkrete API-Nutzung in Phase 2 und sollte vor jeglichem Produktivcode stehen.
2. Danach: CMake-Grundgerüst + Plasmoid-Skelett aufsetzen (Phase 1 Start).

---

*Hinweis: Dieser Plan geht von Plasma 6 / Wayland als Zielplattform aus (KF6, `org.kde.plasma.systemtray`-API). Falls Plasma-5/X11-Support ebenfalls benötigt wird, bitte vor Implementierungsstart Rückmeldung geben — das betrifft v.a. die Plasmoid-Metadata-Version und ggf. KF5-statt-KF6-Abhängigkeiten.*
