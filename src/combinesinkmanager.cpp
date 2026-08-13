#include "combinesinkmanager.h"

#include <QProcess>
#include <QRegularExpression>

#include <utility>

CombineSinkManager::CombineSinkManager(QObject *parent)
    : QObject(parent)
{
}

CombineSinkManager::~CombineSinkManager()
{
    if ((m_module || !m_deviceModules.isEmpty()) && m_controller && m_controller->pwLoop()) {
        pw_thread_loop_lock(m_controller->pwLoop());
        destroyAllLocked();
        pw_thread_loop_unlock(m_controller->pwLoop());
    }
}

void CombineSinkManager::attachController(PipeWireController *controller)
{
    if (m_controller == controller) {
        return;
    }

    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
    }

    m_controller = controller;

    if (m_controller) {
        connect(m_controller, &PipeWireController::audioSinkRegistered,
                this, &CombineSinkManager::handleAudioSinkRegistered);
    }
}

void CombineSinkManager::handleAudioSinkRegistered(quint32 id, const QString &nodeName)
{
    if (nodeName != QLatin1String(kCombineSinkNodeName)) {
        return;
    }

    // Ohne das würde der Combine-Sink zwar korrekt Audio mischen, aber
    // normale Anwendungen (Spotify etc.) spielen weiterhin über das
    // bisherige Standardgerät ab, da sie den neuen Sink nie automatisch
    // wählen (siehe SETUP.md). Feuert jetzt nur noch einmal pro
    // "Einschalt-Sitzung" (der Sink wird für einzelne Geräteänderungen
    // nicht mehr neu erzeugt), nicht mehr bei jeder Auswahländerung.
    QProcess::startDetached(QStringLiteral("wpctl"),
        {QStringLiteral("set-default"), QString::number(id)});
}

bool CombineSinkManager::setActiveTargets(const QVector<Target> &targets)
{
    if (!m_controller || !m_controller->pwContext() || !m_controller->pwLoop()) {
        Q_EMIT errorOccurred(QStringLiteral("Kein laufender PipeWireController verbunden (attachController/start() fehlt)"));
        return false;
    }

    QHash<quint32, int> newState;
    for (const auto &target : targets) {
        newState.insert(target.sink.id, target.delayMs);
    }

    if (newState == m_activeTargetDelays) {
        return true;
    }

    // Beim allerersten Übergang von "keine Ziele" auf "mindestens ein
    // Ziel" dieser Sitzung: die *echte*, gerade aktuelle Standardausgabe
    // merken (bevor wir sie gleich selbst überschreiben) - damit sie sich
    // beim vollständigen Abschalten gezielt wiederherstellen lässt, statt
    // (wie zuvor) irgendein beliebiges Mitglied der Kombi-Auswahl als neuen
    // Dauerzustand zu setzen (Nutzer-Feedback: "findet nicht mehr zurück
    // zum Default-Ausgabegerät", siehe SETUP.md).
    if (m_activeTargetDelays.isEmpty() && !newState.isEmpty()) {
        m_previousDefaultSinkName = queryCurrentDefaultSinkName();
    }

    // Beim Übergang auf "keine Ziele" (zentraler Schalter aus oder letztes
    // Gerät abgewählt): Standard-Ausgabe VOR dem Zerstören des Combine-Sinks
    // zurücksetzen. Ohne das verliert ein gerade aktiver Stream (z.B.
    // Spotify) sein Wiedergabeziel abrupt, sobald der Sink darunter
    // verschwindet, und die PipeWire-Metadaten behalten einen Verweis auf
    // den zerstörten Sink (Zombie-Zustand, siehe SETUP.md).
    if (targets.isEmpty() && !m_activeTargetDelays.isEmpty()) {
        restoreDefaultSink();
    }

    struct pw_thread_loop *loop = m_controller->pwLoop();
    pw_thread_loop_lock(loop);

    bool ok = true;

    // Combine-Sink bei Bedarf erzeugen - nur beim Übergang von 0 auf
    // mindestens 1 Ziel, bleibt für alle weiteren Auswahländerungen
    // bestehen (siehe Klassenkommentar).
    if (!newState.isEmpty() && !m_module) {
        const QByteArray args = buildCombineSinkArgs().toUtf8();
        m_module = pw_context_load_module(m_controller->pwContext(),
                                           "libpipewire-module-combine-stream",
                                           args.constData(),
                                           nullptr);
        ok = (m_module != nullptr);
    }

    // Loopback-Module für weggefallene oder in ihrem Delay geänderte Geräte
    // entfernen (ein geänderter Delay braucht ein Neuladen, da
    // target.delay.sec nur beim Laden gesetzt werden kann).
    for (auto it = m_activeTargetDelays.constBegin(); it != m_activeTargetDelays.constEnd(); ++it) {
        const quint32 id = it.key();
        const bool removed = !newState.contains(id);
        const bool delayChanged = !removed && newState.value(id) != it.value();
        if ((removed || delayChanged) && m_deviceModules.contains(id)) {
            pw_impl_module_destroy(m_deviceModules.take(id));
        }
    }

    // Loopback-Module für neue oder in ihrem Delay geänderte Geräte laden.
    if (ok) {
        for (const auto &target : targets) {
            if (m_deviceModules.contains(target.sink.id)) {
                continue;
            }
            const QByteArray args = buildDeviceLoopbackArgs(target).toUtf8();
            struct pw_impl_module *deviceModule = pw_context_load_module(
                m_controller->pwContext(), "libpipewire-module-loopback",
                args.constData(), nullptr);
            if (!deviceModule) {
                ok = false;
                continue;
            }
            m_deviceModules.insert(target.sink.id, deviceModule);
        }
    }

    // Combine-Sink erst zerstören, nachdem alle Loopback-Module weg sind
    // (sonst würden sie kurzzeitig ins Leere kapturieren).
    if (newState.isEmpty() && m_module) {
        pw_impl_module_destroy(m_module);
        m_module = nullptr;
    }

    pw_thread_loop_unlock(loop);

    if (!ok) {
        Q_EMIT errorOccurred(QStringLiteral("combine-stream/loopback-Modul konnte nicht geladen werden"));
    }

    // Spiegelt den tatsächlich erreichten Zustand wider, nicht nur den
    // angeforderten - falls ein einzelnes Loopback-Modul nicht geladen
    // werden konnte, taucht dessen Gerät hier bewusst nicht als "aktiv" auf
    // (m_deviceModules ist die Quelle der Wahrheit für "existiert wirklich").
    QHash<quint32, int> actualState;
    for (auto it = m_deviceModules.constBegin(); it != m_deviceModules.constEnd(); ++it) {
        actualState.insert(it.key(), newState.value(it.key()));
    }
    m_activeTargetDelays = actualState;
    Q_EMIT activeTargetsChanged(activeTargetIds());
    return ok;
}

QSet<quint32> CombineSinkManager::activeTargetIds() const
{
    QSet<quint32> ids;
    for (auto it = m_activeTargetDelays.constBegin(); it != m_activeTargetDelays.constEnd(); ++it) {
        ids.insert(it.key());
    }
    return ids;
}

bool CombineSinkManager::isActive() const
{
    return m_module != nullptr;
}

QString CombineSinkManager::buildCombineSinkArgs()
{
    return QStringLiteral(
               "{ combine.mode=sink node.name=%1 "
               "node.description=\"Multi-Audio-Output\" "
               "stream.rules=[] }")
        .arg(QLatin1String(kCombineSinkNodeName));
}

QString CombineSinkManager::buildDeviceLoopbackArgs(const Target &target)
{
    const QString label = target.sink.description.isEmpty() ? target.sink.name : target.sink.description;
    const QString baseName = QStringLiteral("multibtaudio_out_") + QString::number(target.sink.id);

    return QStringLiteral(
               "{ node.description=\"Multi-Audio-Output -> %1\" "
               "target.delay.sec=%2 "
               "capture.props={ node.name=%3_in stream.capture.sink=true target.object=%4 } "
               "playback.props={ node.name=%3_out target.object=\"%5\" } }")
        .arg(label)
        .arg(target.delayMs / 1000.0, 0, 'f', 3)
        .arg(baseName)
        .arg(QLatin1String(kCombineSinkNodeName), target.sink.name);
}

QString CombineSinkManager::queryCurrentDefaultSinkName()
{
    // `wpctl inspect` ist der pragmatische, im Projekt bereits etablierte
    // Weg, WirePlumber-Policy-Zustand zu lesen (siehe SETUP.md
    // „Lautstärke-Ergebnis"). Ausgabe enthält u.a. die Zeile
    //     * node.name = "alsa_output.pci-0000_00_1b.0.analog-stereo"
    QProcess process;
    process.start(QStringLiteral("wpctl"), {QStringLiteral("inspect"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
    if (!process.waitForFinished(300)) {
        return {};
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    static const QRegularExpression namePattern(QStringLiteral("node\\.name\\s*=\\s*\"([^\"]+)\""));
    const QRegularExpressionMatch match = namePattern.match(output);
    return match.hasMatch() ? match.captured(1) : QString();
}

void CombineSinkManager::restoreDefaultSink()
{
    // Setzt `default.configured.audio.sink` direkt über die PipeWire-
    // Metadaten zurück, statt über `wpctl set-default <id>`:
    //
    // - Namensbasiert und damit stabil gegenüber wechselnden Node-IDs
    //   (Bluetooth-Reconnects vergeben neue IDs; das frühere ID-basierte
    //   `wpctl set-default` schlug dann still fehl).
    // - Trifft exakt den Metadaten-Key, der den eigentlichen Bug ausmacht:
    //   Wird er nicht zurückgesetzt, zeigt er nach dem Zerstören des
    //   Combine-Sinks dauerhaft auf ein nicht mehr existierendes Gerät.
    //   Plasmas Lautstärke-Applet findet dieses Gerät dann in seiner Liste
    //   nicht wieder und zeigt gar keine Auswahl mehr an ("Sound-Menü
    //   verbuggt", siehe SETUP.md) - `default.audio.sink` wird von
    //   WirePlumber zwar auf ein reales Gerät aufgelöst, das repariert die
    //   Applet-Anzeige aber nicht.
    //
    // Bewusst blockierend (waitForFinished): Der Aufruf muss abgeschlossen
    // sein, bevor der Combine-Sink zerstört wird - ein asynchroner Aufruf
    // verliert das Rennen gegen das direkt anschließende, rein lokale
    // destroyAllLocked() fast immer (siehe SETUP.md „Zentraler Schalter
    // Teil 2").
    QProcess process;
    if (!m_previousDefaultSinkName.isEmpty()) {
        process.start(QStringLiteral("pw-metadata"),
            {QStringLiteral("-n"), QStringLiteral("default"), QStringLiteral("0"),
             QStringLiteral("default.configured.audio.sink"),
             QStringLiteral("{\"name\":\"%1\"}").arg(m_previousDefaultSinkName),
             QStringLiteral("Spa:String:JSON")});
    } else {
        // Kein Ausgangszustand bekannt: Key ersatzlos löschen, dann wählt
        // WirePlumber selbst nach Geräte-Priorität - immer noch besser als
        // einen Verweis auf den gleich zerstörten Combine-Sink stehenzulassen.
        process.start(QStringLiteral("pw-metadata"),
            {QStringLiteral("-n"), QStringLiteral("default"), QStringLiteral("-d"),
             QStringLiteral("0"), QStringLiteral("default.configured.audio.sink")});
    }
    process.waitForFinished(300);
    m_previousDefaultSinkName.clear();
}

void CombineSinkManager::destroyAllLocked()
{
    for (auto *deviceModule : std::as_const(m_deviceModules)) {
        pw_impl_module_destroy(deviceModule);
    }
    m_deviceModules.clear();

    if (m_module) {
        pw_impl_module_destroy(m_module);
        m_module = nullptr;
    }
}
