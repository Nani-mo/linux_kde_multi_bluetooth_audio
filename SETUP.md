# Setup & Infrastruktur-Notizen

Sammlung der Umgebungs-/Infrastruktur-Informationen, die im Projektverlauf per Chat mitgeteilt wurden und nicht Teil der technischen Architektur in [PLAN.md](PLAN.md) sind.

## Entwicklungsumgebung

- **Entwicklungsrechner:** Mac mini (macOS/Darwin), lokaler Benutzername `nanimo`.
- Auf diesem Rechner können PipeWire, KDE Frameworks 6 und die Plasma-Laufzeitumgebung **nicht** kompiliert oder getestet werden (kein Linux). Hier entsteht nur der Quellcode/die Projektstruktur.

## Test-/Zielsystem

- **Linux-Laptop:** CachyOS (Arch-basiert), Hostname `Laptop-Nani-mo`, KDE Plasma 6.7.4 (Wayland/KWin), Kernel 7.1.6-cachyos, Shell: fish.
- **IP im LAN:** `192.168.119.20` (WLAN, `/23`).
- **SSH-Zugriff:** eingerichtet, Auth per Public-Key (derselbe `~/.ssh/id_ed25519` wie für GitLab). Verbindung: `ssh nani@192.168.119.20`. Passwort-Auth war initial nötig, um den Key zu hinterlegen, wird aber nicht mehr benötigt und ist absichtlich nirgends in diesem Repo dokumentiert.
- **Projektverzeichnis auf dem Laptop:** `/home/nani/coding/linux_kde_bluetooth_sharing`.
- **Sichtbarkeit für den Nutzer:** Remote-Aktivitäten laufen in einer tmux-Session namens `claude` auf dem Laptop, mitlesbar via `ssh nani@192.168.119.20` gefolgt von `tmux attach -t claude`.

## Build-Historie auf dem Testsystem

- **Abhängigkeiten:** Bis auf `extra-cmake-modules` (fehlte, per `sudo pacman -S extra-cmake-modules` → 6.28.0 installiert) waren cmake, qt6-base, qt6-declarative, kcoreaddons, kconfig, ki18n, kpackage und pipewire (1.6.8) bereits vorhanden. KF6 insgesamt: 6.28.0.
- **Zwei CMake-Stolpersteine beim Erstbuild gefunden und behoben** (siehe `CMakeLists.txt`):
  1. `QT_DEFAULT_MAJOR_VERSION` muss vor dem ersten `find_package(Qt6 ...)` explizit gesetzt werden, sonst bricht `Qt6QmlMacros.cmake` mit `qt_generate_foreign_qml_types() is only available in Qt 6` ab, obwohl Qt 6 korrekt installiert ist (Bootstrapping-Reihenfolge-Problem der Qt6-CMake-Module).
  2. `multibtaudiocore` (statische Lib mit dem Backend-Code) wird sowohl ins QML-Plugin (Shared Object) als auch ins Konsolentool (Executable) gelinkt — dafür ist projektweit `CMAKE_POSITION_INDEPENDENT_CODE ON` nötig, sonst schlägt der Link des Plugins mit `recompile with -fPIC` fehl.
- **Ergebnis:** `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING_BTAUDIO=ON && cmake --build build` läuft vollständig durch, alle 3 `ctest`-Tests grün, `btaudio-debug` verbindet sich erfolgreich mit dem echten PipeWire-Daemon.
- **Geräteerkennungs-Bug gefunden und behoben:** Die ursprüngliche `PipeWireController`-Implementierung filterte Bluetooth-Sinks anhand der Properties aus dem Registry-`global`-Event. Getestet mit zwei echten Geräten (JBL Flip 6, Soundcore Liberty 4 Pro) zeigte sich: `device.api`/`api.bluez5.*` stehen dort **nicht** zur Verfügung (nur `media.class` u.ä.) — diese Properties liefert PipeWire erst im vollständigen Node-Info nach dem Binden an den Node (`pw_registry_bind` + `pw_node_add_listener`, `info`-Event). `PipeWireController` bindet jetzt jeden `media.class=Audio/Sink`-Kandidaten testweise und entscheidet anhand des Node-Info, ob es sich um ein BT-Gerät handelt. Danach beide Testgeräte korrekt erkannt (inkl. Codec: AAC bzw. SBC, BT-Adresse).

## Phase-0-Spike-Ergebnis: module-combine-stream

Getestet am 11.08. mit zwei echten Geräten: **soundcore Liberty 4 Pro** (AAC, Node-Name `bluez_output.F4_9D_8A_2A_DB_1E.1`) und **JBL Flip 6** (SBC, `bluez_output.F8_5C_7E_AE_FF_05.1`).

- **Ergebnis: erfolgreich.** Simultane, hörbar synchrone Ausgabe auf beiden Geräten vom Nutzer bestätigt.
- **Laden zur Laufzeit funktioniert clientseitig ohne erhöhte Rechte** — getestet über eine normale `pw-cli`-Verbindung (kein sudo/root nötig). Das bestätigt den Primäransatz aus PLAN.md Abschnitt 3.3 für Phase 2.
- **Konfiguration:** `libpipewire-module-combine-stream` mit `combine.mode=sink` und `stream.rules`, die per `node.name`-Match gezielt genau die zwei gewünschten BT-Sinks auswählen (nicht der einfache Beispiel-Wildcard-Match auf `media.class=Audio/Sink`, der auch die eingebauten Lautsprecher erfasst hätte). Ergebnis: ein virtueller Sink (`media.class=Audio/Sink`) mit je einem eigenen `output.*`-Stream-Node pro Zielgerät, korrekt auf `playback_FL`/`playback_FR` der jeweiligen BT-Sinks verlinkt (verifiziert mit `pw-link -l`).
- **Wichtiger Lifecycle-Fund:** Der neu erstellte virtuelle Sink hat `object.register=false` und taucht deshalb **nicht** in der normalen Geräteliste (`wpctl status`, System-Audioeinstellungen) auf. Er lebt nur, solange die ladende Client-Verbindung offen bleibt — beim Trennen (bzw. `pw-cli quit`) wurde er automatisch und rückstandsfrei entfernt. Das passt exakt zur geplanten Architektur (PLAN.md Abschnitt 2): `CombineSinkManager` hält die PipeWire-Verbindung dauerhaft im systray-Hostprozess, nicht in einem kurzlebigen Subprozess.
- **Praktischer Hinweis für die `CombineSinkManager`-Implementierung (Phase 2):** `pw-cli`s Ein-Zeilen-Kommandozeilen-Parsing für `load-module <name> <args>` ist bei mehrzeiligem/verschachteltem SPA-JSON störanfällig (schlug beim Testen mehrfach still fehl, ohne Fehlermeldung). Über die native `pw_core_load_module()`-C-API (so wie es `CombineSinkManager` später tun wird) sollte das kein Problem sein, da dort direkt eine `struct spa_dict`/JSON-Property-Struktur übergeben wird statt eine Kommandozeile geparst werden muss — trotzdem beim Bauen des Properties-Strings auf korrekte SPA-JSON-Syntax achten (Beispiele: `man 7 libpipewire-module-combine-stream`).
- **Getestet, aber noch offen:** Verhalten bei Verbindungsabbruch eines der beiden Geräte während aktiver Kombi-Ausgabe (PLAN.md Phase 2), Latenz-/Drift-Verhalten über längere Zeit bei der AAC/SBC-Mischung (PLAN.md Abschnitt 4).

## Phase-2-Ergebnis: CombineSinkManager

- **Herleitung der API:** `pw-cli`s `load-module`-Befehl (siehe Spike oben) ruft intern `pw_context_load_module(pw_context *, name, args, properties)` auf dem **lokalen** Client-Context auf — nicht über eine Server-RPC-Methode. Das erklärt auch, warum der erzeugte Sink `object.register=false` hatte und an die Lebensdauer der ladenden Verbindung gebunden war. `CombineSinkManager` nutzt daher denselben `pw_context`, den `PipeWireController` bereits für die eigene Verbindung hält (neuer Getter `PipeWireController::pwContext()`/`pwLoop()`), inkl. `pw_thread_loop_lock()`/`unlock()` um den Aufruf, da er aus dem Qt-Hauptthread heraus erfolgt, während der PipeWire-Loop in seinem eigenen Thread läuft.
- **End-to-End-Verifikation mit echter Hardware:** `CombineSinkManager::setActiveTargets()` wurde testweise in `btaudio-debug` eingebaut (danach wieder entfernt) — sobald beide Testgeräte erkannt wurden, hat der eigene Code (nicht mehr manuelles `pw-cli`) den Combine-Sink erstellt, `isActive()` wurde `true`, und der Testton kam wieder hörbar synchron aus JBL Flip 6 und Soundcore Liberty 4 Pro.
- **Bekannte Einschränkung:** Änderungen an der Zielauswahl entladen und laden das Modul komplett neu (kurze Unterbrechung für alle Geräte, nicht nur das neu hinzugefügte/entfernte). Eine unterbrechungsfreie Live-Aktualisierung der `stream.rules` ist noch nicht untersucht.

## Phase-3-Ergebnis: QML-UI

Getestet mit `plasmoidviewer` (aus dem Paket `plasma-sdk`, per pacman installiert) gegen die beiden echten Testgeräte. Nach einem langwierigen Debugging-Marathon lädt und zeigt das Plasmoid die Geräteliste korrekt an (Name, Codec-Untertitel, Checkbox, Lautstärke-Slider pro Gerät), Checkbox-Auswahl steuert live den echten `CombineSinkManager`.

**Drei unabhängige CMake/Qt6-Bugs mussten dafür gefunden werden** (jeweils auf diesem Arch/CachyOS-System reproduziert, evtl. auch auf anderen Distros relevant):

1. **`KDE_INSTALL_QMLDIR` falsch geraten:** KDEInstallDirs setzte `/usr/lib/qt/qml`, tatsächlicher Pfad laut `qmake6 -query QT_INSTALL_QML` ist `/usr/lib/qt6/qml`. Fix: Pfad explizit per `qmake6 -query` abfragen und `KDE_INSTALL_QMLDIR` per `CACHE ... FORCE` überschreiben (siehe CMakeLists.txt).
2. **Fehlende `Qt6::QmlIntegration`-Komponente:** Ohne `find_package(Qt6 ... COMPONENTS ... QmlIntegration)` und Linken von `Qt6::QmlIntegration` verarbeitet moc zwar das `QML_ELEMENT`-Makro (reines `Q_CLASSINFO`) korrekt, aber die für `qmltyperegistrar` nötige Metatypen-JSON-Erzeugung läuft nicht an.
3. **Der eigentliche Hauptschuldige - ECMs eigene `QT_MAJOR_VERSION`-Variable stand auf 5:** `ECMQmlModule.cmake` verzweigt intern nach dieser (von Qt6s `QT_DEFAULT_MAJOR_VERSION` komplett unabhängigen!) Variable zwischen einer Qt5- und einer Qt6-Implementierung (`ECMQmlModule5.cmake` vs. `ECMQmlModule6.cmake`). `QtVersionOption.cmake` ermittelt sie über `if(TARGET Qt5::Core) ... elseif(TARGET Qt6::Core) ...` - auf einem System mit sowohl Qt5 als auch Qt6 installiert (wie diesem: mehrere Qt5-Werkzeuge wie `qmlplugindump`/`qmlimportscanner` liegen unversioniert vor) kollabierte das auf den Qt5-Pfad, obwohl unser Projekt explizit Qt6 anfordert. Symptom: `ecm_add_qml_module()` erzeugte ein oberflächlich gültiges, aber unvollständiges `qmldir` (nur `module`/`plugin`/`classname`, kein `typeinfo`, keine `*_qmltyperegistrations.cpp`) - das Plugin selbst lud einwandfrei per `QPluginLoader` (verifiziert mit einem eigens geschriebenen Testprogramm), wurde von der QML-Engine aber trotzdem mit dem nichtssagenden Fehler `module "..." is not installed` abgelehnt, auch mit explizit korrektem Importpfad. Fix: `set(QT_MAJOR_VERSION 6)` explizit vor `include(ECMQmlModule)` setzen.

**Diagnosemethodik, die zum Fund führte:** `QPluginLoader` direkt gegen die `.so` getestet (lud einwandfrei, IID/className korrekt) → Verdacht auf QML-Modulauflösung statt Plugin-Binary verengt → Vergleich unseres generierten `qmldir` gegen ein bekannt funktionierendes System-Modul (`org.kde.private.kquickcontrols`) zeigte die fehlenden Felder (`typeinfo`, `linktarget`, `prefer`, `depends`) → Quellcode von `ECMQmlModule.cmake` gelesen → Verzweigung nach `QT_MAJOR_VERSION` gefunden → Wert per `message(STATUS ...)`-Debug-Zeile geprüft → Treffer.

**Nebenbefund:** `plasmoidviewer` respektiert `QML2_IMPORT_PATH` nicht (KPackage-Verhalten) - für Tests ohne System-Install wäre stattdessen das Bündeln des Plugins im Plasmoid-Package selbst (`contents/code/`, relativer QML-Import) die sauberere Lösung; aktuell wird stattdessen `sudo cmake --install build` verwendet (Dateien in `build/install_manifest.txt` protokolliert, damit rückgängig machbar).

## Phase-4-Ergebnis: ConfigStore-Persistenz

`DeviceListModel` wurde an `ConfigStore` angebunden (`setConfigStore()`, in `main.qml` verdrahtet): jede Änderung an Active/Volume/DelayMs wird sofort unter `~/.config/plasma-multi-bt-audiorc` in einer Gruppe pro Bluetooth-MAC (`[Device-<MAC>]`) gespeichert; bei erneutem Erkennen desselben Geräts werden die Werte automatisch angewendet, inkl. automatischer Reaktivierung der Kombi-Ausgabe für zuvor aktive Geräte. Mit echter Hardware verifiziert (Häkchen gesetzt → Config-Datei geprüft → `plasmoidviewer` neu gestartet → Gerät kam automatisch wieder aktiv hoch, per `pw-dump` bestätigt).

**Stolperstein bei der Verifikation:** Ein neu hinzugefügtes `Q_INVOKABLE` (`setConfigStore`) schlug in QML zunächst mit `ReferenceError: setConfigStore is not defined` fehl - nicht wegen eines Code-Fehlers, sondern weil `sudo cmake --install build` bei zwei aufeinanderfolgenden Versuchen die `.so` **nicht** aktualisiert hatte (installierte Datei blieb auf einem alten Zeitstempel, obwohl der Build-Output neu war und `-- Installing: ...` in der Ausgabe erschien). Sudo-Cache-Ablauf mitten im Befehl war vermutlich die Ursache. **Lehre:** Nach `cmake --install` bei Debugging-Verdacht immer `ls -la` auf Quell- und Zieldatei vergleichen, nicht nur auf die Install-Log-Zeile vertrauen.

## Lautstärke-Ergebnis: wpctl statt direktem pw_node_set_param()

**Erster Versuch (fehlgeschlagen):** `PipeWireController::setNodeVolume()` sollte die Kanal-Lautstärke direkt per `pw_node_set_param(SPA_PARAM_Props, ...)` auf dem gebundenen Node-Proxy setzen (SPA-Pod mit `SPA_PROP_channelVolumes`, analog zum Combine-Stream-Ansatz). Kompilierte und lief ohne Fehler, hatte aber **keinerlei sichtbaren Effekt** - `wpctl status` zeigte weiterhin den alten Wert.

**Diagnose:** Mit `pw-cli enum-params <id> Props` und `pw-cli set-param <id> Props { channelVolumes = [ ... ] }` manuell verifiziert: Der rohe Wert lässt sich tatsächlich setzen und bleibt auch dauerhaft gespeichert (per erneutem `enum-params` bestätigt) - aber `wpctl status` bleibt davon komplett unberührt, selbst nach einem vom Node emittierten `changed`-Event. **WirePlumber verwaltet die vom restlichen Desktop wahrgenommene Lautstärke offenbar in einem eigenen, von der rohen Node-Property entkoppelten Zustand** (vermutlich Metadata-/Policy-Layer) - ein externer Client kann die rohe Property zwar beschreiben, das hat aber keine Rückwirkung auf das, was WirePlumber/wpctl/die restliche Desktop-UI als „die" Lautstärke ansehen.

**Fix:** `setNodeVolume()` ruft stattdessen `wpctl set-volume <id> <wert>` als Subprozess auf (`QProcess::startDetached`, nicht-blockierend wegen häufiger Slider-Events). Mit echter Hardware verifiziert: Slider im Plasmoid bewegt → `wpctl status` zeigt den neuen Wert. Kleine, bewusste Abweichung von PLAN.md's „keine zusätzliche IPC-Schicht"-Prinzip, aber pragmatisch, da der native Weg nachweislich nicht funktioniert und `wpctl` Teil jeder Plasma-6-Standardinstallation ist.

## Delay-Ergebnis: SPA_PROP_latencyOffsetNsec funktioniert nicht wie erhofft

Nach dem Lautstärke-Fund lag die Vermutung nahe, dass `SPA_PROP_latencyOffsetNsec` ("delay adjustment" laut `spa/param/props.h`) analog per `pw_node_set_param()` echte Verzögerung bewirkt - anders als bei Lautstärke gibt es hierfür keine bekannte WirePlumber-Policy-Ebene, die dagegenwirkt.

- Manuell per `pw-cli set-param <id> Props { latencyOffsetNsec = 50000000 }` getestet: Wert wird akzeptiert und bleibt dauerhaft gesetzt (per `enum-params` bestätigt), Node emittiert ein `changed`-Event.
- Danach in `PipeWireController::setNodeDelay()` implementiert, gebaut, installiert und mit **300 ms** (deutlich hörbar, falls wirksam) mit echten Geräten im Hörtest geprüft: **kein wahrnehmbarer Versatz zwischen den beiden Geräten.**
- **Schlussfolgerung:** Die Property wird zwar vom Node akzeptiert und gespeichert, hat aber offenbar keinen Effekt auf die tatsächliche Audio-Pufferung/Wiedergabe - vermutlich reine Metadaten für Latenz-Berichterstattung/Scheduling, kein „verzögere die Ausgabe"-Schalter. Zudem wurde die Property auf dem rohen BT-Sink-Node gesetzt, nicht auf dem von `module-combine-stream` intern erzeugten Stream-Node pro Zielgerät - dort (bzw. über die `stream.rules`/`create-stream`-Properties beim Modul-Laden) müsste ein funktionierender Ansatz vermutlich ansetzen. Nicht weiter verfolgt in dieser Session; Implementierung wieder entfernt, Delay-Regler ist wieder reine UI-/Datenmodell-Ebene (siehe PLAN.md Phase 3).

## Phase-5-Ergebnis: Idle-Overhead

**Polling-Audit (Code-Review):** Projektweite Suche nach `QTimer`, `sleep`/`usleep`, Endlosschleifen (`while(true)` o.ä.) sowie QML-`Timer{}`-Elementen in `src/`, `tools/` und `plasmoid/` ergab **keinen einzigen Treffer**. Die gesamte Architektur ist ereignisbasiert: `pw_thread_loop` (blockiert intern auf epoll, keine Polling-Schleife), Qt-Signale/Slots zur Weitergabe, QML-Property-Bindings zur Anzeige. Damit ist der PLAN.md-Grundsatz „keine Polling-Loops" für den aktuellen Implementierungsstand erfüllt.

**Idle-Messung (ohne heaptrack/perf, die auf dem Testsystem nicht installiert sind - `/proc/<pid>/stat` und `ps` genügen für den Nachweis):** `btaudio-debug` (enthält denselben `PipeWireController` wie das Plasmoid) mit beiden Testgeräten verbunden gestartet, RSS/CPU-Ticks direkt danach sowie nach 60s reinem Leerlauf verglichen:

| Zeitpunkt | RSS | utime | stime |
|---|---|---|---|
| Direkt nach Start (2 Geräte erkannt) | 33.656 KB | 1 Tick | 0 Ticks |
| Nach 60s Leerlauf | 33.656 KB | 1 Tick | 0 Ticks |

**Exakt identisch** - kein einziger zusätzlicher CPU-Tick, kein Byte RSS-Wachstum über 60s Leerlauf. Bestätigt, dass die Verbindung tatsächlich zero-cost im Leerlauf ist, wenn keine PipeWire-Events eintreffen. ~33 MB RSS für den reinen Backend-Prozess (Qt6Core + PipeWire-Client-Verbindung) erscheint als Baseline plausibel.

**Last-Messung (aktive Kombi-Ausgabe):** `CombineSinkManager` testweise wieder in `btaudio-debug` aktiviert (wie beim Phase-2-Test, danach erneut zurückgebaut), 6× Testton über `paplay --device=multibtaudio_combine` auf beide Geräte abgespielt (~13s aktive Wiedergabe):

| Zeitpunkt | RSS | utime | stime |
|---|---|---|---|
| Direkt nach Aktivierung (vor Wiedergabe) | 35.120 KB | 1 Tick | 1 Tick |
| Nach ~13s aktiver Wiedergabe (beide Geräte) | 37.056 KB | 4 Ticks | 1 Tick |

Nur **30ms zusätzliche CPU-Zeit** über 13s aktiver Zwei-Geräte-Wiedergabe, RSS-Anstieg von ~1,9 MB stabilisiert sich sofort (einmalige Puffer-/Resampler-Allokation, kein kontinuierliches Wachstum/Leck). Erklärung: Die eigentliche Audio-Verarbeitung von `combine-stream` läuft im PipeWire-eigenen Echtzeit-Datenthread innerhalb unseres Prozesses (siehe Phase-0-Spike-Ergebnis oben) - unser Qt-Event-Loop bekommt davon nichts mit, solange keine Struktur-Events (Geräte-Änderungen o.ä.) auftreten.

**Vergleich mit echtem Plasma-Systemtray:** Mit Zustimmung des Nutzers das gebaute Plasmoid testweise über die Plasma-Scripting-API (`qdbus6 org.kde.plasmashell /PlasmaShell org.kde.PlasmaShell.evaluateScript`, offizieller Weg statt manueller Config-Datei-Bearbeitung) live ins echte obere Panel eingefügt (`panel.addWidget("com.nanimo.multibtaudio")`), `plasmashell`-RSS vorher/nachher verglichen, danach wieder sauber per Skript entfernt (`widget.remove()`) und die Wiederherstellung des Original-Panel-Zustands verifiziert:

| Zeitpunkt | plasmashell RSS |
|---|---|
| Vor dem Hinzufügen | 464.688 KB |
| Nach dem Hinzufügen | 459.936 KB |

RSS ist sogar leicht **gesunken** (~4,8 MB) - der Unterschied liegt vollständig innerhalb der normalen Schwankungsbreite eines ~460 MB großen Shell-Prozesses mit vielen aktiven Widgets (u.a. System Monitor, Media Controller, Pager) und ist damit statistisch nicht von Rauschen unterscheidbar. Ein sauberer CPU-Delta-Vergleich war ohne `heaptrack`/`perf` nicht sinnvoll isolierbar (viele andere Panel-Widgets erzeugen selbst kontinuierlich Ticks, z.B. Uhr/System Monitor), die RSS-Messung plus die isolierte Zero-Idle-Messung des Backend-Prozesses oben zusammen ergeben aber ein stimmiges Bild: **kein messbarer Overhead gegenüber einem normalen Systemtray-Icon.**

## Phase-6-Ergebnis: Testmatrix

Nur 2 physische BT-Testgeräte verfügbar (JBL Flip 6, Soundcore Liberty 4 Pro) - 3-4-Geräte-Szenarien aus PLAN.md konnten daher nicht getestet werden, sind aber durch den generischen, listenbasierten Aufbau von `stream.rules` (siehe `CombineSinkManager::buildModuleArgs`) nicht grundsätzlich anders als der 2-Geräte-Fall.

**Verbindungsabbruch während aktiver Kombi-Ausgabe** - mit echter Hardware getestet (`bluetoothctl disconnect`/`connect`, kein manuelles Klicken nötig: `PipeWireController` → `DeviceListModel` → `CombineSinkManager` wie im echten Plasmoid verdrahtet, testweise in `btaudio-debug`, danach zurückgebaut):
- Trennen eines Geräts während aktiver 2-Geräte-Ausgabe → `CombineSinkManager` lädt automatisch mit nur noch 1 Ziel neu (via `DeviceListModel::onSinkRemoved` → `updateCombineTargets`), verbleibendes Gerät spielt unterbrechungsfrei weiter (Testton nur noch auf einem Gerät hörbar, vom Nutzer bestätigt).
- Erneutes Verbinden desselben Geräts → PipeWire vergibt eine **neue Node-ID**, unsere Logik erkennt es dank stabiler Bluetooth-Adresse trotzdem korrekt wieder und reaktiviert die Kombi-Ausgabe.

**Neustart mit aktiver Kombi-Ausgabe** - bereits im Rahmen des Phase-4-ConfigStore-Tests abgedeckt (siehe oben „Phase-4-Ergebnis"): `plasmoidviewer`-Prozess mit aktiver Kombi-Ausgabe hart beendet und neu gestartet → Auswahl wurde aus `ConfigStore` automatisch wiederhergestellt, Kombi-Ausgabe reaktivierte sich ohne manuelles Zutun, keine verwaisten PipeWire-Objekte zurückgeblieben (Modul-Lebenszyklus ist an die Verbindung gebunden, siehe Phase-0-Spike-Ergebnis).

## Nach der Installation: Klick-öffnet-kein-Popup-Bug (echter Panel-Test)

Erst beim ersten echten Test durch den Nutzer im echten Panel (nicht in `plasmoidviewer`, das standardmäßig direkt die volle Ansicht zeigt und die Compact-Repräsentation nie durchlief) aufgefallen: Icon im Panel sichtbar, Hover-Tooltip funktionierte, aber Klick öffnete kein Popup. Drei Ursachen ineinander verschachtelt, jede einzeln gefunden und behoben:

1. **`ReferenceError: deviceList is not defined`** (main.qml:57): `compactRepresentation` und `fullRepresentation` sind getrennte QML-Scopes (eigene Loader/Component-Bäume) - eine `id`, die nur in `fullRepresentation` existiert (`deviceList`, die ListView), ist von `compactRepresentation` aus nicht sichtbar. Die betroffene `totalCount`-Bindung war ohnehin ungenutzt und wurde ersatzlos entfernt.
2. **`Layout.preferredWidth`/`Layout.preferredHeight` auf dem `fullRepresentation`-Root:** Diese Properties wirken nur als Kind eines Layouts - der Popup-Container ist aber keins, wodurch das Popup vermutlich mit Größe 0×0 gerendert wurde. Fix: `implicitWidth`/`implicitHeight`.
3. **Eigentliche Ursache: `Plasmoid.expanded = !Plasmoid.expanded`** - der globale `Plasmoid`-Kontext hat zwar eine `expanded`-Property, aber deren Zuweisung hatte keine Wirkung mehr auf das tatsächliche Popup. Gefunden durch Vergleich mit **allen** auf dem System installierten Plasmoiden (`grep -rl 'Plasmoid.expanded' /usr/share/plasma/plasmoids/`): **kein einziges** verwendet dieses Muster noch. Referenzimplementierung `org.kde.kdeconnect` zeigt das aktuelle Muster: das `PlasmoidItem` wird explizit als `required property PlasmoidItem plasmoidItem` in die Compact-Repräsentation hineingereicht (`compactRepresentation: CompactRepresentation { plasmoidItem: root }`), und `plasmoidItem.expanded` wird getoggelt statt des globalen Kontexts.

**Wichtige Nebenerkenntnis - QML-Änderungen wurden trotz aktualisierter, korrekt installierter Dateien nicht übernommen**, solange nur die Widget-Instanz im Panel entfernt/neu hinzugefügt wurde. Ursache: der laufende `plasmashell`-Prozess hält QML-Komponenten offenbar prozessweit im Speicher gecacht, unabhängig vom Dateiinhalt auf der Platte. Ein vollständiger `plasmashell --replace`-Neustart war nötig, damit Fixes tatsächlich wirksam wurden - reines Entfernen/Neuhinzufügen des Widgets reicht nicht.

**Diagnosemethodik:** `journalctl --user -b | grep multibtaudio` deckte den ersten Fehler auf; danach half der direkte Vergleich mit einer echten, funktionierenden Referenzimplementierung (`org.kde.kdeconnect`) mehr als weiteres Rätselraten - genau wie schon beim `qmldir`-Vergleich in Phase 3.

**Kleiner Nebenbefund (kein Bug):** Nutzer berichtete ein kurzes „Mikro-Lag" (~30s) beim (Wieder-)Öffnen des Popups. `pw-mon` über 6s zeigte keine ungewöhnliche PipeWire-Event-Frequenz für unsere Nodes; CPU-Sampling von `plasmashell` zeigte keinen klar zuordenbaren Ausschlag. Vermutlich einmaliges QML-JIT-/Shader-Aufwärmen beim Neu-Instanziieren von `fullRepresentation` auf der eher schwachen integrierten Grafik (Intel HD 5500) des Testsystems - kein Code-Bug, nicht weiter verfolgt auf Wunsch des Nutzers.

## Kombi-Sink wird nicht zur Standard-Ausgabe (echter Alltagstest mit Spotify)

Nutzer-Test mit Spotify nach der Panel-Installation deckte auf: Checkboxen funktionierten technisch (Kombi-Sink wurde korrekt erzeugt, Lautstärke ließ sich einstellen), aber **normale Anwendungen spielten trotzdem nur über ein einzelnes Gerät ab**. Ursache: Ein neu erzeugter PipeWire-Sink wird dadurch, dass er existiert, noch nicht automatisch zur System-Standardausgabe - normale Apps (Spotify, Browser, …) senden Audio immer an das aktuell konfigurierte Standardgerät (das, was im normalen KDE-Lautstärke-Icon eingestellt ist), niemals automatisch an einen neuen, unbekannten Sink. Unser Combine-Sink war für die normale Geräteauswahl unsichtbar (`object.register=false`, siehe Phase-0-Ergebnis) und wurde nie als Standard gesetzt.

**Fix:** `PipeWireController` meldet jetzt jeden neu erscheinenden `Audio/Sink`-Node über ein neues Signal `audioSinkRegistered(id, nodeName)` (Erweiterung des bereits vorhandenen Registry-Listeners, kein zusätzlicher PipeWire-Code nötig). `CombineSinkManager` hört darauf, erkennt seinen eigenen Sink am Namen (`multibtaudio_combine`) und setzt ihn per `wpctl set-default <id>` (gleiches Subprozess-Muster wie bei der Lautstärke) automatisch als System-Standard, sobald er geladen wird. **Mit Spotify auf echter Hardware verifiziert:** beide Geräte spielten danach gleichzeitig.

**Nebenbefund (Test-Hygiene):** Da `test_combinesinkmanager` jetzt real `wpctl set-default` aufruft, ändert ein Testlauf kurzzeitig den echten System-Standard des Testrechners. Nach Testende (Modul wird entladen) fällt PipeWire/WirePlumber automatisch und sauber auf das vorherige Gerät zurück (verifiziert: Standard vor und nach `ctest`-Lauf identisch) - kein bleibender Seiteneffekt.

**Werkzeug-Stolperstein (Sync):** Ein `rsync --exclude='src'`-Aufruf (gedacht, um `makepkg`s Build-Artefakt-Verzeichnis `src/` auszuschließen) hat versehentlich auch das echte Quellcode-Verzeichnis `src/` des Projekts ausgeschlossen, da das Pattern nicht am Pfad-Anfang verankert war (`--exclude='/src'` wäre korrekt gewesen). Mehrere aufeinanderfolgende `rsync`-Aufrufe meldeten fälschlich Erfolg, ohne die eigentlichen Dateiänderungen zu übertragen - erst ein direkter `diff` zwischen lokaler und entfernter Datei deckte das auf. Workaround: `scp` für die betroffenen Dateien direkt.

## Git-Server

- **Selbstgehosteter GitLab-Server** im lokalen Netzwerk: `http://192.168.20.2:8088`
- **GitLab-Benutzer/Namespace:** `nani_mo`
- **Projekt-Repository:** `http://192.168.20.2:8088/nani_mo/plasma-multi-bt-audio`
  - Als leeres Projekt angelegt (ohne automatisch generiertes README/.gitignore/License seitens GitLab, da lokal bereits vorhanden).

## Git-Auth

- **Tatsächlich funktionierender Weg (vom Laptop aus verifiziert): HTTP + Personal Access Token**, nicht SSH. GitLab läuft in einem Docker-Container auf einem NAS (`skynas.intra.skyhomes.de`, reverse-DNS von `192.168.20.2`); die vom „Clone"-Button gezeigte SSH-URL nennt einen intern nicht auflösbaren Docker-Container-Hostnamen (`ec10eb8470a5`), und Port 22 auf der echten IP gehört zum NAS-eigenen System-SSH, nicht zu GitLabs internem `gitlab-shell` — ein `git push` über SSH landete deshalb beim falschen SSH-Dienst und fragte nach einem (nicht existierenden) Passwort. Port-Scan (`nmap`) der üblichen alternativen GitLab-SSH-Ports (2222, 8022, 8443, 10022) fand keinen offenen Port - der korrekte gemappte Port ist nirgends von außen ersichtlich, sondern nur über die NAS-eigene Docker/Container-Manager-Oberfläche einsehbar.
- **Funktionierende Remote-URL:** `http://192.168.20.2:8088/nani_mo/plasma-multi-bt-audio.git` (die bekannte, im Browser genutzte Adresse - nicht die vom „Clone"-Button vorgeschlagene interne Hostname-Variante).
- **Auth:** GitLab-Benutzername (`nani_mo`) als Username, ein **Personal Access Token** (User Settings → Access Tokens → „Legacy token", Scope `write_repository`) als Passwort. Vom Laptop aus erzeugt und erfolgreich gepusht (initialer Commit, Branch `main`).
- Ein separater `~/.ssh/id_ed25519`-Key wurde auf dem Laptop zwar erzeugt (für den ursprünglich geplanten SSH-Weg), wird aber für den Git-Zugriff aktuell nicht genutzt.

## Workflow-Entscheidungen

- Der Nutzer möchte die eigentlichen Git-Kommandos (`init`, `add`, `commit`, `remote add`, `push`, …) **selbst ausführen**, um Git zu lernen. Der Assistent bereitet nur die Dateien/Struktur vor und führt keine Git-Befehle selbstständig aus.
- Lizenzwahl: **GPL-3.0-or-later** — Nutzer hat freie Wahl dem Assistenten überlassen, Standardwahl für KDE-Anwendungen.
- Projekt-/Plasmoid-Namensgebung wurde dem Assistenten überlassen:
  - Projektname/Repo: `plasma-multi-bt-audio`
  - Plasmoid-ID (reverse-domain-Stil): `com.nanimo.multibtaudio`
  - Beides jederzeit umbenennbar (nur Konfigurationswerte).

## Offene Punkte

- [x] Hostname/IP des CachyOS-Laptops im LAN mitgeteilt.
- [x] SSH-Zugriff vom Mac mini auf den CachyOS-Laptop eingerichtet (Public-Key-Auth), für direktes Remote-Bauen/Testen durch den Assistenten.
- [x] Exakte Clone-URL bestätigt — SSH-Port ließ sich nicht ermitteln, stattdessen HTTP + Personal Access Token als funktionierender Weg etabliert (siehe „Git-Auth" oben). Repo ist auf dem GitLab-Server gepusht (Branch `main`).
- [x] Build-Abhängigkeiten auf dem Laptop verifiziert und fehlende installiert (siehe „Build-Historie auf dem Testsystem" oben).
