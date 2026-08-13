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
- [x] Dynamisches Hinzufügen/Entfernen einzelner Ziele: **in Phase 7.2 grundlegend neu gelöst** (siehe dort) — statt komplettem Entladen+Neuladen bei jeder Änderung gibt es jetzt einen dauerhaften Combine-Sink plus ein unabhängiges Loopback-Modul pro Zielgerät; einzelne Geräte hinzufügen/entfernen lädt nur noch deren eigenes Modul, ohne den Sink oder andere Geräte anzufassen.
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

### Phase 7 — Stabilisierung & UI-Upgrades (nächste Phase, in Bearbeitung)

Reihenfolge bewusst gewählt: erst Lags/Bugs beheben (7.1), danach UI-Upgrades (7.2) — neue Funktionsfläche auf instabilem Grund macht Fehlersuche später schwerer.

#### 7.1 Lags & Bugs beheben

- [x] **Delay-Regler funktionsfähig machen.** Ursache war zweigeteilt (siehe SETUP.md „Delay-Ergebnis Teil 2"): (1) `DeviceListModel::setData()` reichte `DelayMsRole`-Änderungen nie an `CombineSinkManager` weiter — reiner Verdrahtungsbug, unabhängig vom Audiopfad-Problem. (2) `module-combine-stream` bietet laut `man 7` keinen manuellen Delay-Parameter (nur automatischen `combine.latency-compensate`-Ausgleich) — `libpipewire-module-loopback` dagegen hat ein dokumentiertes `target.delay.sec` (seit PipeWire 0.3.60). **Mit echter Hardware verifiziert** (Paket 0.1.0-7, siehe SETUP.md „Erster echter Live-Test"): Nutzer bestätigt hörbaren, größenordnungsrichtigen Versatz (0ms = kein Versatz, 500ms ≈ eine halbe Sekunde) — Feature funktioniert erstmals wie vorgesehen. Ursprünglich als zweistufiger Delay-Proxy umgesetzt (combine-stream zielt auf einen zwischengeschalteten Loopback-Sink) - im Zuge des Architektur-Umbaus in Phase 7.2 (siehe SETUP.md „Checkbox-Toggle stoppt überall") vereinfacht: jedes Zielgerät hat jetzt ohnehin ein eigenes Loopback-Modul, `target.delay.sec` ist dort einfach eine Property davon, kein separater Proxy-Schritt mehr nötig.
- [x] **Mikro-Lag beim (Wieder-)Öffnen des Popups untersuchen.** Per Code-Analyse geprüft (siehe SETUP.md „Lag-Review-Ergebnis") und danach **mit echter Hardware verifiziert**: Nutzer berichtet nach Paket 0.1.0-7, dass ihm kein Lag mehr aufgefallen ist. Kein abschließender Beweis für die Warmup-These aus der Vorsession, aber kein spürbares Problem mehr.
- [x] **Allgemeiner Lag-/Bug-Review-Durchgang.** Zwei Lag-Quellen per Code-Analyse gefunden (siehe SETUP.md „Lag-Review-Ergebnis"): Lautstärke-Slider und Delay-SpinBox lösten pro Interaktions-Tick einen `wpctl`-Subprozess-Spawn bzw. ein volles `combine-stream`-Reload aus. Der erste Fix-Versuch beim Lautstärke-Regler (100ms-Debounce, `restart()` bei jedem `onMoved`) erzeugte im Live-Test einen **neuen, vom Nutzer gefundenen Bug**: kleine Slider-Bewegungen wurden kaum wirksam, weil jede neue Bewegung den Timer vor dem Auslösen erneut aufschob. Fix: Debounce durch Throttle ersetzt — `repeat: true`-`Timer` läuft während `pressed`, wendet alle 60ms den aktuellen Wert an, plus sofortige Anwendung beim Loslassen (`onPressedChanged`). Zusätzlich vom Nutzer gemeldeter UI-Bug behoben: Der Hover-Tooltip des Tray-Icons erschien sofort beim Hovern und blockierte dabei den Klickpfad zum Öffnen des Popups — ersatzlos entfernt (`CompactRepresentation.qml`).
- [ ] Lautstärke-Regler-Throttle-Fix noch nicht mit echter Hardware verifiziert (Paket 0.1.0-8 steht aus) — nächster Schritt.

#### 7.2 UI-Upgrades

- [x] **Zentraler Ein/Aus-Schalter für das gesamte Tool.** `QQC2.Switch` oben in der vollen Popup-Ansicht ([plasmoid/contents/ui/main.qml](plasmoid/contents/ui/main.qml)), gebunden an eine neue `DeviceListModel::toolEnabled`-Property. Bei `false` liefert `updateCombineTargets()` bewusst eine leere Zielliste an `CombineSinkManager` (baut die Kombi-Ausgabe komplett ab), ohne die per-Geräte-`active`/`volume`/`delayMs`-Entries oder deren `ConfigStore`-Persistenz anzufassen — beim Zurückschalten kommt der vorherige Zustand automatisch wieder. Zustand selbst wird global (nicht pro Gerät) über eine neue `ConfigStore::toolEnabled()/setToolEnabled()`-Methode in einer eigenen `General`-Konfigurationsgruppe persistiert. Tray-Icon-Reaktion brauchte keinen separaten Code: `activeCount` fällt bei ausgeschaltetem Schalter automatisch auf 0, wodurch `CompactRepresentation.qml` schon vorhandene Muted-Icon-Logik greift. **Nutzer-Live-Test (Paket 0.1.0-11/0.1.0-13) fand drei echte Bugs:** (1) Schalter hatte keinerlei Wirkung — `setToolEnabled()` fehlte `Q_INVOKABLE`, dadurch war die Methode zwar als `Q_PROPERTY`-Setter verdrahtet, aber nicht als aus QML aufrufbare Funktion sichtbar (`deviceModel.setToolEnabled(checked)` lief ins Leere); der ursprüngliche C++-Unit-Test hatte das nicht gefangen, da er die Methode direkt statt über das Meta-Objekt-System aufrief — behoben plus neuer Regressionstest `toolEnabledIsInvokableFromQml` über `QMetaObject::invokeMethod`-nach-Name (siehe SETUP.md „Zentraler Ein/Aus-Schalter: Q_INVOKABLE vergessen"). (2) Beide Schalterzustände waren optisch kaum unterscheidbar — Label zeigt den Zustand jetzt zusätzlich als Text an. (3) Ausschalten pausierte die laufende Musik (Einschalten dagegen nicht) — ursprüngliche Annahme, WirePlumber würde den Default-Sink beim Verschwinden des Kombi-Sinks von selbst unterbrechungsfrei neu zuweisen, war **falsch**: ein aktiver Stream verliert sein Ziel abrupt, wenn der Sink darunter zerstört wird, viele Apps pausieren dann automatisch. Fix: `CombineSinkManager::setActiveTargets()` setzt bei leerer Zielliste die Standardausgabe jetzt explizit per `wpctl set-default` auf eines der bisherigen Zielgeräte zurück, *bevor* der Combine-Sink zerstört wird (siehe SETUP.md „Zentraler Schalter Teil 2") — funktionierte bei YouTube/Browser, **nicht bei Spotify** (vermutlich Spotify-eigene "Ausgabegerät verschwunden"-Pauselogik, außerhalb der Kontrolle dieses Projekts). Beim Testen dieses Fixes zusätzlich einen vierten, gravierenderen Bug gefunden: Checkbox-Toggle einzelner Geräte stoppte die Wiedergabe auf **allen** Geräten — Ursache war ein grundlegendes Architekturproblem (jede Auswahländerung zerstörte den kompletten Combine-Sink), behoben durch einen Architektur-Umbau (dauerhafter Sink + unabhängige Loopback-Module pro Gerät, siehe SETUP.md „Checkbox-Toggle stoppt überall" und den aktualisierten Klassenkommentar in `combinesinkmanager.h`). **Mit echter Hardware bestätigt: Checkbox-Toggle einzelner Geräte funktioniert jetzt unterbrechungsfrei.** Einmalig (nicht reproduzierbar) berichtete der Nutzer, dass nach einem Aus-/Wiedereinschalten des zentralen Schalters ein Gerät fehlte und manuell nachgetriggert werden musste - gezielte Diagnose mit Logging über zwei weitere Testdurchläufe zeigte beide Geräte jedes Mal korrekt wiederhergestellt; gilt als nicht bestätigt/nicht reproduzierbar, siehe SETUP.md „Re-Enable-Bug".
- [x] **Längere Lautstärkeregler.** `Slider`-Breite pro Gerät in [plasmoid/contents/ui/DeviceItem.qml](plasmoid/contents/ui/DeviceItem.qml) von `gridUnit * 6` auf `gridUnit * 10` vergrößert; Popup-Breite in `main.qml` von `gridUnit * 20` auf `gridUnit * 24` angepasst, damit der Gerätename nicht übermäßig eingekürzt wird.
- [x] **Funktionierende Delay-Regler** — Backend-Fix aus 7.1, mit echter Hardware verifiziert (siehe oben). Wertebereich nachträglich auf Nutzerwunsch vergrößert: 500ms wirkte im Hörtest zwar richtig proportioniert, aber subjektiv klein für die "große Zahl" - SpinBox jetzt 0-2000ms statt vorher 0-500ms, Schrittweite bewusst bei 5ms belassen (`DeviceItem.qml`). Bei 2000ms laut Nutzer „auf jeden Fall deutlich hörbar".
- [x] **Test-Ton-Knopf zur Delay-Kalibrierung** (Nutzerwunsch). Umgesetzt als globaler Button unter dem zentralen Lautstärkeregler (`main.qml`), ruft `PipeWireController::playTestTone()`. Technischer Ansatz: das Klick-Signal (kurzer, schnell abklingender ~2kHz-Impuls, 6x wiederholt mit Pausen) wird selbst als rohes PCM erzeugt (`buildTestTonePcm()`, kein mitgeliefertes Audio-Asset nötig) und per Stdin an `pw-cat -p --raw ...` durchgereicht - mit echtem `pw-cat` gegen den laufenden Daemon verifiziert (Abspieldauer entsprach exakt der erzeugten PCM-Länge, sowohl per Datei als auch per Stdin-Pipe getestet). Läuft über die System-Standardausgabe, dadurch automatisch über den Combine-Sink verteilt, wenn die Kombi-Ausgabe aktiv ist - inklusive der individuellen Delay-Einstellung pro Gerät. Button deaktiviert, wenn `toolEnabled=false`. Nicht-blockierend (`QProcess`, kein `waitForFinished`), Prozess räumt sich selbst per `deleteLater()` nach Abspielende auf.
- [x] **Zentraler Lautstärkeregler** (Nutzerwunsch). Erste Implementierung steuerte die Lautstärke des gemeinsamen `multibtaudio_combine`-Sinks selbst (`CombineSinkManager::setMasterVolume()`) - im Live-Test mit echter Hardware **wirkungslos für Spotify/Browser**: Diese verbinden über die PulseAudio-Kompatibilitätsschicht mit eigener, vom Ziel-Sink unabhängiger Stream-Lautstärke, die auf Sink-Lautstärkeänderungen nicht reagiert (mit einem Parallelvergleich - eigener nativer Testton vs. Spotify gleichzeitig - eindeutig verifiziert, siehe SETUP.md „GELÖST: Zentraler Lautstärkeregler wirkungslos"). **Neu umgesetzt:** Der Regler multipliziert sich stattdessen mit der Pro-Gerät-Lautstärke jedes aktiven Geräts (`DeviceListModel::applyEffectiveVolume()`, effektive Lautstärke = Geräte-Lautstärke × Master-Lautstärke) und nutzt denselben `PipeWireController::setNodeVolume()`-Mechanismus auf die *reale* Geräte-Node, der für die Pro-Gerät-Regler bereits nachweislich funktioniert - `CombineSinkManager::setMasterVolume()`/`m_combineSinkId` wieder entfernt, nicht mehr gebraucht. `DeviceListModel::masterVolume`/`setMasterVolume()` (Q_PROPERTY, **mit `Q_INVOKABLE`** - Lektion aus dem `toolEnabled`-Bug direkt beachtet) persistiert weiterhin global in `ConfigStore` (`MasterVolume` in der `General`-Gruppe). Regler in `main.qml` mit demselben Throttle-statt-Debounce-Muster wie die Pro-Gerät-Regler, deaktiviert wenn `toolEnabled=false`. Unit-Test `masterVolumeIsInvokableFromQmlAndClamped` weiterhin gültig (testet nur die `DeviceListModel`-Property, nicht die inzwischen entfernte `CombineSinkManager`-Implementierung). **Vom Nutzer im echten Widget bestätigt: wirkt jetzt hörbar.** Nachfrage des Nutzers, ob der Combine-Sink stattdessen im KDE-eigenen Sound-Menü mit funktionierendem Lautstärkeregler auftauchen könnte, kurz eingeordnet (Sichtbarkeit dort vermutlich schon gegeben, ein dort funktionierender Regler wäre aber technisch derselbe bereits verworfene Ansatz) - auf Nutzerwunsch nicht umgesetzt, siehe SETUP.md.
- [x] **Tray-Icon-Tooltip entfernt** (Nutzer-Feedback: blockierte den Klickpfad zum Öffnen des Popups, siehe 7.1). Vorgezogen aus dieser UI-Aufgabe, da eng mit dem Tray-Icon selbst verwandt — die eigentliche Icon-Ersetzung unten ist davon unabhängig noch offen.
- [x] **Alle ausgabefähigen Audiogeräte anzeigen, nicht nur Bluetooth.** `PipeWireController::isBluetoothAudioSink()` zu `isEligibleOutputSink()` umgebaut ([src/pipewirecontroller.cpp](src/pipewirecontroller.cpp)): akzeptiert jetzt alle `media.class=Audio/Sink`-Nodes, schließt aber explizit die eigenen virtuellen Sinks aus (`node.name`-Präfix `multibtaudio_` - Combine-Sink und Delay-Proxys aus Phase 7.1 sind selbst `Audio/Sink`-Nodes und müssten sonst als eigene Auswahlziele auftauchen, mit echtem `pw-dump` verifiziert, siehe SETUP.md). Mit `btaudio-debug` **live gegen den echten Daemon verifiziert**, während die eigene Kombi-Ausgabe parallel aktiv lief: eingebauter Laptop-Lautsprecher (`alsa_output...analog-stereo`) erscheint jetzt zusätzlich zu den beiden BT-Geräten, `multibtaudio_combine`/`multibtaudio_delay_81` erscheinen korrekt nicht.
  - `DeviceListModel`: Geräte-Icon je nach Typ umgesetzt (`DeviceItem.qml`) — `device.form-factor` erwies sich laut echtem `pw-dump` auf dem Testsystem als für **kein** Gerät gesetzt (auch nicht Bluetooth), daher primär anhand `btAddress`-Präsenz unterschieden (Bluetooth vs. nicht) und `formFactor` nur als Verfeinerung genutzt, falls doch geliefert.
  - `CombineSinkManager` brauchte keine Änderung — bereits generisch genug (matcht nur per `node.name`, unabhängig von Geräteart).
  - Persistenz erweitert: `ConfigStore`/`DeviceListModel` nutzen jetzt `btAddress` falls vorhanden, sonst den PipeWire-`node.name` als Fallback-Schlüssel für Nicht-Bluetooth-Geräte (die keine MAC haben).
  - [x] Naming-Konsequenz geklärt: Nutzer hat sich für **„Multi-Audio-Output"** als neuen Anzeigenamen entschieden, Umfang bewusst auf `metadata.json` (`Name`/`Name[de]`, jetzt auch `Description`/`Description[de]`) begrenzt — Plasmoid-ID (`com.nanimo.multibtaudio`), Paketname (`plasma-multi-bt-audio`) und GitLab-Projektname bleiben unverändert (Nutzer würde eine vollständige Umbenennung selbst über Git erledigen wollen, siehe HANDOFF.md „Workflow-Hinweise"). Leerzustand-Text in `main.qml` ebenfalls generalisiert („No audio output devices found").
- [x] **Besseres Icon für den Systemtray.** Eigenes symbolisches SVG-Icon erstellt (`icons/sc-status-multiaudiooutput-symbolic.svg`): zwei Lautsprecher-Formen nebeneinander statt einem einzelnen, im selben `.ColorScheme-Text`/`fill="currentColor"`-Stil wie Breezes eigene Icons (Farbe passt sich automatisch an Farbschema/Panel an). Ein erster, filigranerer Entwurf (Lautsprecher + zwei dünne Linien zu kleinen Punkten) erwies sich bei echter Tray-Größe (22px, mit `rsvg-convert` gerendert und visuell geprüft) als unlesbares Gekritzel - zu viele dünne Details überleben das Verkleinern nicht. Die "zwei nebeneinander"-Lösung ist bei 128px und bei echten 22px klar lesbar (beide Male gerendert und geprüft). Installation über `ecm_install_icons()` (ECM-Standardmodul, Namenskonvention `<größe>-<gruppe>-<name>.<ext>`) nach `share/icons/hicolor/scalable/status/` - das ist der Ort, an dem auch andere Plasmoids (`org.kde.plasma.folder`, `kdeconnect`) ihre Icons ablegen; ein Icon-Name im Plasmoid-Package selbst (`plasmoid/contents/icons/`, wie ursprünglich hier notiert) wird von der Icon-Namen-Auflösung in `metadata.json`/`Kirigami.Icon` **nicht** gefunden. Mit `tar -tf` am gebauten Paket verifiziert, dass die Datei am erwarteten Pfad landet. Nur der "aktiv"-Zustand bekommt das neue Icon; für "aus" bleibt bewusst das Standard-Mute-Icon (`audio-volume-muted-symbolic`), das Konzept ist bereits universell klar. Noch nicht mit echtem Panel-Rendering (Plasmas eigene Farbschema-Substitution) gegengetestet.
- [ ] Nach UI-Änderungen: `plasmashell --replace` (Cache-Neustart nötig, siehe HANDOFF.md), dann echten Panel-Test inkl. Klick-auf-Tray-Icon-Pfad.

## 8. Risiken & offene Punkte

| Risiko | Auswirkung | Umgang |
|---|---|---|
| `pw_core_load_module` clientseitig eingeschränkt/verboten | Primärer Ansatz nicht direkt umsetzbar | Fallback: WirePlumber-Lua-Konfigurationsdatei dynamisch schreiben + WirePlumber-Reload, oder Subprozess `pw-cli` |
| Audio-Drift zwischen Geräten unterschiedlicher Codecs | Hörbare Sync-Probleme | Nutzeraufklärung + manueller Delay-Slider (siehe Abschnitt 4), kein Vollautomatik-Fix in v1 |
| Plasma-Systray-API-Änderungen zwischen Plasma-Minor-Versionen | Breaking Changes bei Plasma-Updates | An aktueller Plasma-6-LTS-Version entwickeln, Systray-Spezifikation (`org.kde.plasma.systemtray`) einhalten |
| WirePlumber-Konfiguration variiert je Distribution | Node-Metadaten-Namen (`device.api`, `api.bluez5.*`) könnten leicht abweichen | Robuste Filterung mit Fallback-Heuristiken, gegen mehrere Distros testen (Kubuntu, Arch, Fedora KDE) |

## 9. Nächste Schritte

**Stand (Ende der Sitzung, Paket 0.1.0-23):** Phase 7.1 (Lags & Bugs) **und** Phase 7.2 (UI-Upgrades) sind **vollständig abgeschlossen** — zentraler Ein/Aus-Schalter, längere Lautstärkeregler, funktionierender Delay-Regler, „alle Ausgabegeräte anzeigen" statt nur Bluetooth, Umbenennung zu „Multi-Audio-Output", zentraler Lautstärkeregler, Test-Ton-Knopf zur Delay-Kalibrierung und ein eigenes Tray-Icon. Alle Punkte mit echter Hardware verifiziert und vom Nutzer im echten Widget bestätigt. Der ursprünglich gewünschte Funktionsumfang ist damit komplett abgedeckt, plus zahlreiche im Laufe mehrerer Sitzungen gefundene und behobene Bugs (siehe SETUP.md für die vollständige Chronik).

**⚠️ Wichtigster Punkt für den Einstieg morgen - unbedingt zuerst lesen:** `.gitignore` enthielt bis eben fälschlich eine Regel, die das komplette `src/`-Quellcode-Verzeichnis von Git ausschloss (Namenskollision mit `makepkg`s internem `$srcdir`) - **der gesamte C++-Code war in der gesamten bisherigen Repo-Historie nie committet.** Das ist jetzt behoben (`.gitignore` korrigiert, `PKGBUILD` baut nach `$startdir/build` statt `$srcdir/build`), siehe SETUP.md „KRITISCH GELÖST: `src/`-Namenskollision". **Falls `git status` erneut kein `src/` zeigt oder ein Push seltsam wenig Dateien enthält, zuerst dort nachlesen**, bevor an Code-Bugs gedacht wird.

**Bekannte, akzeptierte Einschränkung (kein offener Bug):** Spotify pausiert beim vollständigen Ausschalten des zentralen Schalters (YouTube/Browser nicht) — vermutlich Spotify-eigene "Ausgabegerät verschwunden"-Sicherheitslogik, von hier aus nicht beeinflussbar.

**Nicht reproduzierbarer Einzelfall (im Auge behalten, nicht aktiv verfolgen):** Ein einziges Mal fiel nach Aus-/Wiedereinschalten des zentralen Schalters ein Gerät aus der Kombi-Ausgabe heraus. Zwei gezielte Nachtests mit Diagnose-Logging zeigten beide Geräte jedes Mal korrekt wiederhergestellt - siehe SETUP.md „Re-Enable-Bug" für das Logging-Muster, falls es erneut auftritt.

**Damit ist kein bekannter offener Punkt aus dem ursprünglichen Anforderungskatalog mehr übrig.** Mögliche Anschlussarbeit, falls vom Nutzer gewünscht: weitere Distributionen testen (siehe Abschnitt 8, Risikotabelle), `.deb`/RPM-Packaging, oder ganz neue Funktionswünsche, die noch nicht besprochen wurden - dafür gibt es aktuell keine vorbereitete Planung, erst mit dem Nutzer klären.

---

*Hinweis: Dieser Plan geht von Plasma 6 / Wayland als Zielplattform aus (KF6, `org.kde.plasma.systemtray`-API). Falls Plasma-5/X11-Support ebenfalls benötigt wird, bitte vor Implementierungsstart Rückmeldung geben — das betrifft v.a. die Plasmoid-Metadata-Version und ggf. KF5-statt-KF6-Abhängigkeiten.*
