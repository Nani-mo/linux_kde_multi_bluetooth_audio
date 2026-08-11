# Übergabe an eine lokale Claude-Code-Session auf diesem Laptop

Dieses Dokument ist für eine **neue Claude-Code-Session, die direkt auf diesem CachyOS-Laptop läuft** (nicht mehr über SSH von einem Mac aus). Die gesamte bisherige Entwicklung lief remote von einem Mac mini aus (siehe SETUP.md „Entwicklungsumgebung") — das hat sich ab jetzt geändert: **dieser Laptop ist die alleinige Entwicklungsumgebung**, der Mac wird für dieses Projekt nicht mehr angefasst.

Lies zuerst in dieser Reihenfolge:

1. **README.md** — Kurzüberblick, Bauen, Installieren.
2. **PLAN.md** — Vollständige Architektur, Tech-Stack, Phasen-Checkliste mit Status (Phasen 0–6 sind fertig und mit echter Hardware verifiziert, siehe unten).
3. **SETUP.md** — Infrastruktur-Notizen **und** eine detaillierte Chronik aller in dieser Session gefundenen Bugs samt Ursache und Fix. Das ist die wertvollste Datei hier — bevor du ein Problem debuggst, das nach einem PipeWire/Qt6/Plasma-Eigenheit aussieht, dort nachsehen, ob es nicht schon gelöst wurde.

## Was jetzt sofort wichtig ist

**plasmashell cacht QML/Plugin-Änderungen prozessweit.** Nach jeder Code-Änderung (egal ob C++ im Plugin oder QML im Plasmoid-Package) reicht es **nicht**, das Widget im Panel zu entfernen und neu hinzuzufügen — die Änderung wird trotzdem ignoriert. Es braucht einen echten Neustart:

```bash
plasmashell --replace &
```

(Danach kurz warten, bis der Prozess wieder läuft: `pgrep -x plasmashell`.)

**`plasmoidviewer -a plasmoid`** (aus dem bereits installierten Paket `plasma-sdk`) ist nützlich zum schnellen Testen, zeigt aber standardmäßig direkt die volle Ansicht und **umgeht den Klick-auf-Icon-Pfad komplett**. Ein Bug, der genau dort saß (`Plasmoid.expanded` wirkungslos, siehe SETUP.md), wurde dadurch lange nicht gefunden. Für alles, was mit dem Tray-Icon selbst zu tun hat, **immer zusätzlich im echten Panel testen**.

## Alle SSH/tmux/sudo-Hinweise in SETUP.md sind jetzt obsolet

Große Teile von SETUP.md (tmux-Session-Handling, sudo-Passwort-Timing, `qdbus6`-Fernsteuerung von plasmashell, Screenshot-Workarounds über `spectacle` + `scp`) waren nötig, weil die Entwicklung **remote von einem Mac aus per SSH** lief. Das entfällt komplett: du läufst jetzt lokal, hast direkten Terminal-, Display- und `sudo`-Zugriff. Diese Abschnitte sind nur noch als Kontext/Historie relevant, nicht als Anleitung für dein weiteres Vorgehen.

## Aktueller Stand (bei Übergabe)

- Installiertes Paket: `plasma-multi-bt-audio` **0.1.0-6** (`pacman -Qi plasma-multi-bt-audio` zum Prüfen). Bei jeder Code-Änderung `pkgrel` in `PKGBUILD` erhöhen, dann `makepkg -sfi`.
- Widget ist aktuell **nicht** dauerhaft im echten Panel (wurde für Tests hinzugefügt/entfernt) — ggf. selbst über Rechtsklick aufs Panel → „Widgets hinzufügen" → „Multi-BT-Audio" wieder hinzufügen.
- Zwei reale Bluetooth-Testgeräte sind gekoppelt und einsatzbereit:
  - **JBL Flip 6** — `F8:5C:7E:AE:FF:05`, Codec SBC
  - **Soundcore Liberty 4 Pro** — `F4:9D:8A:2A:DB:1E`, Codec AAC
- **Funktioniert und ist mit echter Hardware + Spotify verifiziert:** Geräteliste, Checkbox-Auswahl (steuert echte Kombi-Ausgabe **und** setzt sie korrekt als System-Standardausgabe, siehe SETUP.md „Kombi-Sink wird nicht zur Standard-Ausgabe"), Lautstärkeregler (über `wpctl set-volume`), Persistenz pro Geräte-MAC, automatische Wiederherstellung bei Reconnect, automatisches Abfangen von Verbindungsabbrüchen.
- **Offen / nicht gelöst:** Delay-Offset-Regler wirkt nicht (UI vorhanden, schreibt nur ins Datenmodell). Ein Ansatz über `SPA_PROP_latencyOffsetNsec` direkt auf dem BT-Sink-Node wurde versucht und wieder verworfen (siehe SETUP.md „Delay-Ergebnis") — nächster Versuch wäre vermutlich über die `stream.rules`/`create-stream`-Properties beim Laden von `module-combine-stream` selbst, nicht nachträglich extern.
- Build/Test-Zyklus ist Standard: `cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING_BTAUDIO=ON && cmake --build build && ctest --test-dir build --output-on-failure`.
- Config-Datei zur Laufzeit: `~/.config/plasma-multi-bt-audiorc`.

## Workflow-Hinweise vom Nutzer (weiterhin gültig)

- Der Nutzer führt Git-Kommandos (`init`, `add`, `commit`, `push`, …) **selbst** aus, um Git zu lernen — nicht proaktiv für ihn übernehmen.
- **Repo ist auf dem GitLab-Server gepusht** (Branch `main`, Remote `http://192.168.20.2:8088/nani_mo/plasma-multi-bt-audio.git`, Auth per Personal Access Token statt SSH — siehe SETUP.md „Git-Auth" für die Begründung). `PKGBUILD` baut weiterhin aus dem lokalen Arbeitsverzeichnis, nicht von der Git-Quelle.
- `.claude/` ist gitignored und lokal an die jeweilige Session/Maschine gebunden — nicht Teil des Projekts.
