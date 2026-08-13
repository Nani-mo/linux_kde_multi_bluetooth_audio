#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QString>

extern "C" {
#include <pipewire/pipewire.h>
#include <spa/utils/hook.h>
}

/**
 * Hält eine persistente, ereignisbasierte Verbindung zum PipeWire-Daemon
 * in einem eigenen Thread (pw_thread_loop) und meldet über Qt-Signale,
 * wenn Audio-Ausgabegeräte (alle media.class=Audio/Sink-Nodes, nicht nur
 * Bluetooth - siehe PLAN.md Phase 7.2 "Alle ausgabefähigen Audiogeräte")
 * erscheinen, verschwinden oder sich ändern. Eigene virtuelle Sinks
 * (Combine-Sink, Delay-Proxys, siehe CombineSinkManager) werden dabei
 * explizit ausgeschlossen. Siehe PLAN.md Abschnitt 3.1.
 */
class PipeWireController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    struct SinkInfo {
        quint32 id = 0;
        QString name;
        QString description;
        QString btAddress;
        QString codec;
        QString formFactor;
    };

    explicit PipeWireController(QObject *parent = nullptr);
    ~PipeWireController() override;

    PipeWireController(const PipeWireController &) = delete;
    PipeWireController &operator=(const PipeWireController &) = delete;

    /** Baut die Verbindung zum PipeWire-Daemon auf und startet den Loop-Thread. */
    Q_INVOKABLE bool start();

    /** Beendet den Loop-Thread und gibt alle PipeWire-Ressourcen frei. */
    void stop();

    bool isRunning() const;

    /**
     * Setzt die Lautstärke (0.0-1.0) des PipeWire-Nodes mit der angegebenen
     * ID über `wpctl set-volume` (Subprozess statt direktem
     * pw_node_set_param(): WirePlumber verwaltet die vom Desktop
     * wahrgenommene Lautstärke separat von der rohen Node-Property, siehe
     * SETUP.md). Nicht-blockierend (startDetached) - Rückgabewert sagt nur,
     * ob der Prozess gestartet werden konnte, nicht ob wpctl selbst
     * erfolgreich war.
     */
    Q_INVOKABLE bool setNodeVolume(quint32 id, qreal volume);

    /**
     * Spielt einen kurzen, mehrfach wiederholten Klick-Testton über die
     * System-Standardausgabe ab (PLAN.md Phase 7.2, Nutzerwunsch) - dient
     * zur Delay-Kalibrierung: läuft die Kombi-Ausgabe, landet der Ton auf
     * allen aktiven Geräten gleichzeitig, sodass sich ein Versatz beim
     * Verstellen des Delay-Reglers direkt heraushören lässt, ohne eigene
     * Musik laufen haben zu müssen. Erzeugt das Signal selbst als rohes
     * PCM (kein mitgeliefertes Audio-Asset nötig) und reicht es per Stdin
     * an `pw-cat` durch - mit echtem `pw-cat` gegen den laufenden Daemon
     * verifiziert (Abspieldauer entsprach exakt der erzeugten PCM-Länge).
     * Nicht-blockierend, läuft im Hintergrund weiter.
     */
    Q_INVOKABLE bool playTestTone();

    /**
     * Roher Zugriff auf Context/Loop für CombineSinkManager: Module wie
     * combine-stream werden per pw_context_load_module() in den lokalen
     * Client-Context geladen (siehe PLAN.md Phase-0-Spike-Ergebnis in
     * SETUP.md) - dafür braucht CombineSinkManager beide Handles sowie die
     * Möglichkeit, sich mit dem Loop-Thread zu synchronisieren.
     */
    struct pw_context *pwContext() const { return m_context; }
    struct pw_thread_loop *pwLoop() const { return m_loop; }

    // PipeWire-Callbacks: müssen als freie/statische Funktionszeiger in die
    // C-API eingehängt werden, daher public statt private (kein Zugriff aus
    // Namespace-Scope auf private Static-Member möglich).
    static void registryGlobal(void *data, uint32_t id, uint32_t permissions,
                                const char *type, uint32_t version,
                                const struct spa_dict *props);
    static void registryGlobalRemove(void *data, uint32_t id);
    static void coreError(void *data, uint32_t id, int seq, int res, const char *message);
    static void nodeInfo(void *data, const struct pw_node_info *info);

Q_SIGNALS:
    void sinkAdded(const PipeWireController::SinkInfo &sink);
    void sinkRemoved(quint32 id);
    void sinkChanged(const PipeWireController::SinkInfo &sink);
    void connectionError(const QString &message);

    /**
     * Feuert für jeden neu registrierten Audio/Sink-Node (nicht auf
     * Bluetooth-Geräte beschränkt wie sinkAdded) - genutzt von
     * CombineSinkManager, um die Node-ID des selbst erzeugten Combine-Sinks
     * zu finden, damit dieser per wpctl als System-Standardausgabe gesetzt
     * werden kann (siehe SETUP.md „Kombi-Sink wird nicht zur Standard-
     * Ausgabe").
     */
    void audioSinkRegistered(quint32 id, const QString &nodeName);

private:
    // Die Registry liefert im "global"-Event nur die Registrierungs-Properties
    // eines Nodes (u.a. media.class). Weitere Properties wie device.api und
    // api.bluez5.* (falls Bluetooth) stehen erst im vollständigen Node-Info,
    // das nur nach dem Binden an den Node über den info-Event verfügbar ist
    // - daher hält jede Kandidaten-Sink (media.class=Audio/Sink) eine eigene
    // Proxy-/Listener-Bindung, bis geklärt ist, ob sie als Ausgabegerät
    // zählt (nicht einer unserer eigenen virtuellen Sinks ist, siehe
    // isEligibleOutputSink). spa_hook-Adressen müssen stabil bleiben
    // (PipeWire verlinkt sie intrusiv), daher heap-allokiert statt in einem
    // Value-Container.
    struct NodeBinding {
        PipeWireController *self = nullptr;
        quint32 id = 0;
        struct pw_proxy *proxy = nullptr;
        struct spa_hook listener {};
        bool confirmedEligible = false;
    };

    static bool isEligibleOutputSink(const struct spa_dict *props);
    static SinkInfo sinkInfoFromProps(quint32 id, const struct spa_dict *props);
    static QByteArray buildTestTonePcm();

    void handleNodeInfo(NodeBinding *binding, const struct pw_node_info *info);
    void destroyBinding(quint32 id);

    struct pw_thread_loop *m_loop = nullptr;
    struct pw_context *m_context = nullptr;
    struct pw_core *m_core = nullptr;
    struct pw_registry *m_registry = nullptr;
    struct spa_hook m_registryListener {};
    struct spa_hook m_coreListener {};
    QHash<quint32, NodeBinding *> m_nodeBindings;
    bool m_running = false;
};
