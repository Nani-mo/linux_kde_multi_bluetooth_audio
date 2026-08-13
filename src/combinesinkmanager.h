#pragma once

#include "pipewirecontroller.h"

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QString>
#include <QVector>

extern "C" {
#include <pipewire/impl-module.h>
}

/**
 * Steuert das eigentliche Multi-Output-Routing. Architektur (PLAN.md Phase
 * 7.2 "Checkbox-Toggle stoppt überall" - Nutzer-Live-Test-Fund, siehe
 * SETUP.md für die Herleitung):
 *
 * - EIN dauerhafter virtueller Sink ("multibtaudio_combine"), erzeugt per
 *   libpipewire-module-combine-stream mit *leeren* stream.rules - das
 *   Modul erzeugt trotzdem einen stabilen Audio/Sink-Node samt Monitor-
 *   Ports (mit echtem pw-cli gegen den laufenden Daemon verifiziert). Wird
 *   nur erzeugt, wenn mindestens ein Ziel aktiv ist, und nur zerstört, wenn
 *   wieder keines mehr aktiv ist - bleibt für Änderungen an der
 *   Geräteauswahl dazwischen komplett unangetastet.
 * - PRO aktivem Zielgerät ein eigenes, unabhängiges
 *   libpipewire-module-loopback, das vom Monitor des Combine-Sinks
 *   kapturiert (`stream.capture.sink=true`) und auf das reale Gerät
 *   wiedergibt - inklusive `target.delay.sec` für die Delay-Kompensation
 *   (Parameter seit PipeWire 0.3.60, siehe `man 7
 *   libpipewire-module-loopback`; SPA_PROP_latencyOffsetNsec hatte
 *   dagegen nachweislich keine Wirkung, siehe SETUP.md „Delay-Ergebnis").
 *   Ein einzelnes Gerät hinzuzufügen/zu entfernen lädt nur dessen eigenes
 *   Loopback-Modul neu - alle anderen Geräte UND der Combine-Sink selbst
 *   bleiben dabei ununterbrochen aktiv (behebt den oben verlinkten Bug:
 *   vorher wurde bei jeder Änderung der komplette Combine-Sink zerstört
 *   und neu geladen, wodurch jeder daran hängende Stream - z.B. Spotify -
 *   sein Ziel abrupt verlor).
 *
 * Bekannte verbleibende Einschränkung: Der Combine-Sink selbst wird beim
 * Übergang auf "keine Ziele mehr" (letztes Gerät abgewählt oder zentraler
 * Schalter aus) weiterhin zerstört - das ist unvermeidbar, da dann nichts
 * mehr zu kombinieren ist. Manche Apps (beobachtet: Spotify, nicht aber
 * Browser/YouTube) pausieren dabei automatisch, vermutlich eine App-eigene
 * "Ausgabegerät verschwunden"-Sicherheitslogik, die von hier aus nicht
 * beeinflussbar ist - siehe SETUP.md.
 */
class CombineSinkManager : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    /** Ziel-Sink plus gewünschter Delay-Offset in Millisekunden. */
    struct Target {
        PipeWireController::SinkInfo sink;
        int delayMs = 0;
    };

    explicit CombineSinkManager(QObject *parent = nullptr);
    ~CombineSinkManager() override;

    CombineSinkManager(const CombineSinkManager &) = delete;
    CombineSinkManager &operator=(const CombineSinkManager &) = delete;

    /** Verbindet den Manager mit einem laufenden PipeWireController. */
    Q_INVOKABLE void attachController(PipeWireController *controller);

    /** Ersetzt die aktive Ziel-Sink-Auswahl (inkl. Delay pro Gerät) durch die übergebenen Geräte. */
    bool setActiveTargets(const QVector<Target> &targets);

    QSet<quint32> activeTargetIds() const;
    bool isActive() const;

Q_SIGNALS:
    void activeTargetsChanged(const QSet<quint32> &sinkIds);
    void errorOccurred(const QString &message);

private:
    // node.name des von uns erzeugten Combine-Sinks (siehe
    // buildCombineSinkArgs). Wird auch genutzt, um den per
    // PipeWireController::audioSinkRegistered gemeldeten eigenen Sink
    // wiederzuerkennen und per wpctl als System-Standardausgabe zu setzen -
    // sonst bleibt der Sink zwar aktiv, aber normale Apps (Spotify o.ä.)
    // spielen weiter über das bisherige Standardgerät ab (siehe SETUP.md).
    static constexpr auto kCombineSinkNodeName = "multibtaudio_combine";

    static QString buildCombineSinkArgs();
    static QString buildDeviceLoopbackArgs(const Target &target);
    static QString queryCurrentDefaultSinkName();
    void restoreDefaultSink();
    void destroyAllLocked();
    void handleAudioSinkRegistered(quint32 id, const QString &nodeName);

    PipeWireController *m_controller = nullptr;
    struct pw_impl_module *m_module = nullptr;
    // Ein unabhängiges Loopback-Modul pro aktivem Zielgerät (Schlüssel:
    // reale PipeWire-Node-ID des Geräts, siehe SinkInfo::id).
    QHash<quint32, struct pw_impl_module *> m_deviceModules;
    QHash<quint32, int> m_activeTargetDelays;
    // node.name der System-Standardausgabe von *vor* der ersten Aktivierung
    // dieser Sitzung (leer = unbekannt) - wird beim vollständigen Abschalten
    // gezielt wiederhergestellt (siehe restoreDefaultSink). Bewusst der
    // *Name* statt der Node-ID: PipeWire-Node-IDs sind nicht stabil (ein
    // Bluetooth-Reconnect zwischen Ein- und Ausschalten vergibt neue IDs),
    // ein darauf basierendes `wpctl set-default <id>` schlägt dann still
    // fehl und hinterlässt den Zombie-Zustand aus SETUP.md
    // („KDE-Sound-Menü verbuggt"). node.name ist dagegen stabil.
    QString m_previousDefaultSinkName;
};
