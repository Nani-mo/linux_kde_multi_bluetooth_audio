# Übergabe an eine neue Claude-Code-Session auf diesem Laptop

Dieses Dokument ist der **Einstiegspunkt für jede neue Session**. Es beschreibt den aktuellen Stand; die vollständige Historie steht in PLAN.md und SETUP.md.

Lies zuerst in dieser Reihenfolge:

1. **Dieses Dokument** — aktueller Stand, Fallstricke, Workflow.
2. **PLAN.md** — Architektur, Tech-Stack, Phasen-Checkliste mit Status (Abschnitt 9 „Nächste Schritte" hat immer den aktuellsten Kurzüberblick).
3. **SETUP.md** — Infrastruktur-Notizen **und** eine detaillierte Chronik aller gefundenen Bugs samt Ursache und Fix. Das ist die wertvollste Datei hier — bevor du ein Problem debuggst, das nach einer PipeWire/Qt6/Plasma-Eigenheit aussieht, dort nachsehen, ob es nicht schon gelöst wurde.

## ⚠️ Zuerst prüfen: `git status` zeigt `src/` als Änderung/unversioniert?

Falls `git status` **kein** `src/` erwähnt (weder als „modified" noch als „untracked"), ist wahrscheinlich die alte `.gitignore`-Regel zurück oder ein neuer Klon/Checkout hat sie nie gesehen. `src/` enthält den **gesamten C++-Quellcode** dieses Projekts - ohne ihn ist das Repo nicht baubar. Details und warum das passieren konnte: SETUP.md „KRITISCH GELÖST: `src/`-Namenskollision".

## Was jetzt sofort wichtig ist

**plasmashell cacht QML/Plugin-Änderungen prozessweit.** Nach jeder Code-Änderung (egal ob C++ im Plugin oder QML im Plasmoid-Package) reicht es **nicht**, das Widget im Panel zu entfernen und neu hinzuzufügen — die Änderung wird trotzdem ignoriert. Es braucht einen echten Neustart:

```bash
plasmashell --replace &
```

(Danach kurz warten, bis der Prozess wieder läuft: `pgrep -x plasmashell`.)

**Der Standard-Zyklus für eine Code-Änderung** ist inzwischen eingespielt:

```bash
cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure
# pkgrel in PKGBUILD erhöhen, dann:
makepkg -sf --noconfirm
pkexec pacman -U --noconfirm ./plasma-multi-bt-audio-0.1.0-<pkgrel>-x86_64.pkg.tar.zst
plasmashell --replace &
```

`pkexec` öffnet dabei einen grafischen Passwortdialog auf dem Desktop (`sudo` funktioniert in einer nicht-interaktiven Session **nicht**, es kann kein Passwort abfragen).

**Vor jedem `plasmashell --replace`/jeder Installation:** kurz prüfen, ob gerade echte Musik über das Tool läuft (`wpctl status`), und falls ja, den Nutzer vorher fragen - beides unterbricht die laufende Wiedergabe kurz.

**`plasmoidviewer -a plasmoid`** ist nützlich zum schnellen Testen, **umgeht aber den Klick-auf-Icon-Pfad komplett**. Für alles rund ums Tray-Icon immer zusätzlich im echten Panel testen.

## Live-Diagnose: `journalctl`, nicht Datei-Redirect

**`plasmashell`s `qWarning()`/`console.warn()`-Ausgaben landen im systemd-User-Journal, nicht zuverlässig in einer per `plasmashell --replace > log.txt 2>&1` umgeleiteten Datei.** Für Live-Debugging:

```bash
journalctl --user -f --no-pager | grep --line-buffered -i "multibtaudio"
```

Das war in der letzten Sitzung entscheidend, um einen Bug zu finden, der sich nicht anders reproduzieren ließ.

## Audio-Bugs ohne Bluetooth-Hardware reproduzieren

Man braucht die echten BT-Geräte **nicht**, um Routing-/Default-Sink-/Lautstärke-Probleme zu untersuchen. Virtuelle Sinks als Fake-Geräte anlegen und den Zyklus per `pw-cli` nachstellen:

```bash
pactl load-module module-null-sink sink_name=testdev_a sink_properties=device.description=TestDevA
# ... testen ...
pactl unload-module <von load-module ausgegebene ID>
```

Für Lautstärke-Fragen: eigenen Testton per `pw-cat` erzeugen und parallel zu echter Musik abspielen, dabei Lautstärke ändern und vergleichen, ob beides oder nur eines reagiert (deckt PulseAudio-Kompat-Layer-Probleme auf, siehe SETUP.md „Zentraler Lautstärkeregler wirkungslos").

Nützliche Diagnose-Kommandos:

```bash
wpctl status                                     # PipeWire-Sicht
pactl info | grep "Default Sink"                 # Pulse-Sicht
pactl list sinks short                           # zeigt auch Sinks, die wpctl als "Filter" einordnet
pw-metadata -n default                           # die entscheidenden Default-Keys
wpctl inspect @DEFAULT_AUDIO_SINK@               # node.name der aktuellen Standardausgabe
```

**Fallstrick:** `pw-cli` stürzt beim Beenden über eine Pipe ab und bleibt als Prozess mit hoher CPU-Last hängen — nach solchen Tests immer `pgrep -ax pw-cli` prüfen und aufräumen. Ebenso: angelegte Null-Sinks wieder entladen.

## Aktueller Stand

- Installiertes Paket: **0.1.0-25** (`pacman -Qi plasma-multi-bt-audio` zum Prüfen).
- Anzeigename ist **„Multi-Audio-Output"** (Plasmoid-ID `com.nanimo.multibtaudio`, Paketname `plasma-multi-bt-audio` und GitLab-Projektname bewusst unverändert).
- Reale Bluetooth-Testgeräte sind gekoppelt (können wechseln, zuletzt gesehen: JBL Flip 6, JBL Go 3, Soundcore Liberty 4 Pro).
- **Phase 7.1 und Phase 7.2 (siehe PLAN.md) sind vollständig abgeschlossen.** Alles Folgende ist mit echter Hardware verifiziert und vom Nutzer bestätigt:
  - Geräteliste zeigt alle Ausgabegeräte, nicht nur Bluetooth.
  - Checkbox-Auswahl, unterbrechungsfreies Zu-/Abschalten einzelner Geräte während laufender Wiedergabe.
  - Pro-Gerät-Lautstärkeregler.
  - **Zentraler Lautstärkeregler** - multipliziert sich mit der Pro-Gerät-Lautstärke jedes aktiven Geräts (nicht über den Combine-Sink selbst, siehe „Wichtige Architektur-Lektion" unten).
  - Funktionierender Delay-Regler (0–2000 ms, über `target.delay.sec` eines Loopback-Moduls pro Gerät).
  - Test-Ton-Knopf zur Delay-Kalibrierung (`PipeWireController::playTestTone()`, erzeugt einen Klick als rohes PCM, spielt ihn per `pw-cat` ab).
  - Eigenes Tray-Icon (`icons/sc-status-multiaudiooutput-symbolic.svg`, zwei Lautsprecher statt einem).
  - Zentraler Ein/Aus-Schalter, Persistenz, automatische Wiederherstellung bei Reconnect, Abfangen von Verbindungsabbrüchen.
  - KDE-Sound-Menü zeigt nach Ausschalten wieder korrekt ein Gerät als ausgewählt an (siehe SETUP.md „KDE-Sound-Menü verbuggt").
- **Bekannte, akzeptierte Einschränkung (kein zu behebender Bug):** Spotify pausiert beim vollständigen Ausschalten des zentralen Schalters (Browser/YouTube nicht) - vermutlich Spotify-eigene Sicherheitslogik.
- **Nicht reproduzierbarer Einzelfall:** Einmalig fiel nach einem Aus-/Wiedereinschalten ein Gerät aus der Kombi-Ausgabe - zwei gezielte Nachtests zeigten es danach immer korrekt. Falls es wiederkehrt: Diagnose-Logging-Muster in SETUP.md „Re-Enable-Bug".
- **Ebenfalls nicht reproduzierbar:** Meldung, ein eingestellter Delay-Wert werde nach Aus-/Wiedereinschalten unwirksam. Diagnose-Logging zeigte durchgehend korrekte Werte; der fehlgeschlagene Testlauf hatte eine stummgeschaltete, alleinstehende Sink als Testgerät. Erneuter Test durch den Nutzer bestätigte korrektes Verhalten. Details: SETUP.md „Delay-Regler nach Wiedereinschalten „unwirksam"".
- **Bekannte Testeinschränkung:** `ctest`s `fullCycleLeavesNoStaleDefaultSink` kollidiert mit dem echten Plasmoid, falls dessen Kombi-Ausgabe *währenddessen* ebenfalls aktiv ist (beide nutzen den Sink-Namen `multibtaudio_combine`) - vor Testläufen `wpctl status | grep multibtaudio_combine` prüfen, siehe SETUP.md.
- Config-Datei zur Laufzeit: `~/.config/plasma-multi-bt-audiorc`.

## Wichtige Architektur-Lektion: Sink-Lautstärke ≠ App-Lautstärke

Eine PipeWire-Node-Eigenschaft (z.B. Lautstärke) auf unserem **eigenen synthetischen Combine-Sink** zu setzen, wird von Spotify, den meisten Browsern und praktisch allen Apps, die über die **PulseAudio-Kompatibilitätsschicht** (`pipewire-pulse`) verbinden, schlicht **ignoriert** - die haben eine eigene, vom Ziel-Sink unabhängige Stream-Lautstärke. Nur direkt angebundene native PipeWire-Clients reagieren darauf. Deshalb steuert der zentrale Lautstärkeregler stattdessen die **Pro-Gerät-Lautstärke** der *realen* Zielgeräte (die honorieren ihre eigene Lautstärke zuverlässig). Details, Diagnosemethode (Parallelvergleich mit eigenem Testton) und die vollständige Herleitung: SETUP.md „GELÖST: Zentraler Lautstärkeregler wirkungslos". Bei jeder neuen Funktion, die eine Node-Eigenschaft setzt und von echten Apps wahrgenommen werden soll: vorher mit genau dieser Methode gegentesten, nicht nur mit einem eigenen Testwerkzeug.

## Architektur-Kern (seit dem Umbau in Phase 7.2)

Wichtig zu verstehen, bevor man an `CombineSinkManager` arbeitet:

- **Ein dauerhafter** `multibtaudio_combine`-Sink (via `module-combine-stream` mit *leeren* `stream.rules`). Wird nur beim Übergang 0→1 Ziel erzeugt und beim Übergang auf 0 Ziele zerstört — **nicht** bei einzelnen Geräteänderungen.
- **Pro Zielgerät ein eigenes** `module-loopback`, das vom Monitor des Combine-Sinks kapturiert und aufs reale Gerät ausgibt (inkl. `target.delay.sec`).
- Dadurch berührt das Zu-/Abschalten eines einzelnen Geräts weder den Sink noch die anderen Geräte — vorher wurde bei jeder Änderung alles neu geladen, was die Wiedergabe überall abriss.
- Lautstärke wird **nicht** am Combine-Sink gesetzt, sondern pro Gerät an der jeweils realen Ziel-Node (siehe Architektur-Lektion oben).

## Workflow-Hinweise vom Nutzer (weiterhin gültig)

- Der Nutzer führt Git-Kommandos (`init`, `add`, `commit`, `push`, …) **selbst** aus, um Git zu lernen — nicht proaktiv für ihn übernehmen. Datei-Vorbereitung (z.B. `.gitignore`-Fixes) ist ok, das eigentliche Committen/Pushen macht der Nutzer.
- **Repo ist auf dem GitLab-Server gepusht** (Branch `main`, Remote `http://192.168.20.2:8088/nani_mo/plasma-multi-bt-audio.git`, Auth per Personal Access Token statt SSH — siehe SETUP.md „Git-Auth"). `PKGBUILD` baut weiterhin aus dem lokalen Arbeitsverzeichnis, nicht von der Git-Quelle.
- `.claude/` ist gitignored und lokal an die jeweilige Session/Maschine gebunden — nicht Teil des Projekts.
- Vor Aktionen, die die laufende Wiedergabe unterbrechen (`plasmashell --replace`, Paketinstallation), kurz Bescheid geben bzw. nachfragen, wenn gerade Musik läuft.
