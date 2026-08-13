#pragma once

#include "combinesinkmanager.h"
#include "configstore.h"
#include "pipewirecontroller.h"

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QVector>

/**
 * Bindet die von PipeWireController erkannten Audio-Ausgabegeräte (alle
 * Audio/Sink-Nodes, nicht nur Bluetooth - siehe PLAN.md Phase 7.2) als
 * flache Liste an QML. Siehe PLAN.md Abschnitt 3.2.
 *
 * Hält außerdem die aktuelle "aktiv in Kombi-Ausgabe"-Auswahl (ActiveRole)
 * und reicht sie bei jeder Änderung automatisch an CombineSinkManager
 * weiter (siehe updateCombineTargets) - dadurch wird auch ein während
 * aktiver Kombi-Ausgabe getrenntes Gerät automatisch aus dem Combine-Sink
 * entfernt, ohne dass die QML-Seite das explizit behandeln müsste.
 *
 * Ist ein ConfigStore angebunden (setConfigStore), werden Active-/Volume-/
 * DelayMs-Änderungen pro Gerät persistiert (per Bluetooth-MAC, falls
 * vorhanden, sonst per PipeWire-Node-Name als Fallback für Nicht-Bluetooth-
 * Geräte) und bei Wiedersehen desselben Geräts automatisch vorgeschlagen
 * (siehe PLAN.md Phase 4).
 */
class DeviceListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
    Q_PROPERTY(bool toolEnabled READ toolEnabled WRITE setToolEnabled NOTIFY toolEnabledChanged)
    Q_PROPERTY(qreal masterVolume READ masterVolume WRITE setMasterVolume NOTIFY masterVolumeChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        FormFactorRole,
        BtAddressRole,
        CodecRole,
        ActiveRole,
        VolumeRole,
        DelayMsRole,
    };
    Q_ENUM(Role)

    explicit DeviceListModel(QObject *parent = nullptr);

    Q_INVOKABLE void setController(PipeWireController *controller);
    Q_INVOKABLE void setCombineSinkManager(CombineSinkManager *manager);
    Q_INVOKABLE void setConfigStore(ConfigStore *store);

    /** Komfort-Wrapper für QML: setData() über eine QModelIndex ist dort umständlich. */
    Q_INVOKABLE bool setActive(int row, bool active);
    Q_INVOKABLE bool setVolume(int row, qreal volume);
    Q_INVOKABLE bool setDelayMs(int row, int delayMs);

    int activeCount() const { return m_activeCount; }

    /**
     * Zentraler Ein/Aus-Schalter (PLAN.md Phase 7.2): bei false wird die
     * Kombi-Ausgabe komplett abgebaut (CombineSinkManager erhält eine leere
     * Zielliste), ohne die Geräteauswahl/-lautstärke/-delay in den Entries
     * oder im ConfigStore zu verwerfen - beim Wiedereinschalten kommt der
     * vorherige Zustand automatisch zurück.
     */
    bool toolEnabled() const { return m_toolEnabled; }
    // Q_INVOKABLE nötig, obwohl die Methode schon als Q_PROPERTY-WRITE
    // dient: Ein reiner Property-Setter ist in QML *nicht* automatisch als
    // aufrufbare Funktion (deviceModel.setToolEnabled(x)) sichtbar - beide
    // Export-Mechanismen sind im Meta-Objekt-System getrennt. Ohne das lief
    // main.qmls onToggled-Aufruf ins Leere (Bug, mit Nutzer-Testfeedback
    // gefunden, siehe SETUP.md).
    Q_INVOKABLE void setToolEnabled(bool enabled);

    /**
     * Zentraler Lautstärkeregler (PLAN.md Phase 7.2), zusätzlich zu den
     * Pro-Gerät-Reglern - global persistiert (unabhängig von der
     * Geräteauswahl, analog toolEnabled).
     *
     * Wirkt als Multiplikator auf die Pro-Gerät-Lautstärke jedes aktiven
     * Geräts (effektive Lautstärke = entry.volume * m_masterVolume),
     * angewendet über PipeWireController::setNodeVolume() auf die reale
     * Geräte-Node-ID - nicht (wie ursprünglich versucht) über die
     * Lautstärke des gemeinsamen Combine-Sinks selbst. Grund: Apps, die
     * über die PulseAudio-Kompatibilitätsschicht verbinden (Spotify,
     * die meisten Browser - praktisch der Normalfall), haben eine eigene,
     * vom Ziel-Sink unabhängige Stream-Lautstärke und reagieren auf eine
     * Sink-Lautstärkeänderung gar nicht; nur direkt angebundene native
     * PipeWire-Clients (z.B. unser eigener Testton) taten das - mit
     * echter Hardware im Parallelvergleich verifiziert (siehe SETUP.md
     * „Zentraler Lautstärkeregler wirkungslos"). Reale Zielgeräte
     * (ALSA/Bluetooth-Adapter) honorieren ihre eigene Lautstärke dagegen
     * zuverlässig, siehe die bereits funktionierenden Pro-Gerät-Regler.
     */
    qreal masterVolume() const { return m_masterVolume; }
    Q_INVOKABLE void setMasterVolume(qreal volume);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QHash<int, QByteArray> roleNames() const override;

Q_SIGNALS:
    void activeCountChanged();
    void toolEnabledChanged();
    void masterVolumeChanged();

private:
    struct Entry {
        PipeWireController::SinkInfo sink;
        bool active = false;
        qreal volume = 1.0;
        int delayMs = 0;
    };

    void onSinkAdded(const PipeWireController::SinkInfo &sink);
    void onSinkRemoved(quint32 id);
    void onSinkChanged(const PipeWireController::SinkInfo &sink);

    int indexForId(quint32 id) const;
    void updateCombineTargets();
    void persistEntry(const Entry &entry);
    void applyEffectiveVolume(const Entry &entry);
    void applyEffectiveVolumesForActiveEntries();

    PipeWireController *m_controller = nullptr;
    CombineSinkManager *m_combineSinkManager = nullptr;
    ConfigStore *m_configStore = nullptr;
    QVector<Entry> m_entries;
    int m_activeCount = 0;
    bool m_toolEnabled = true;
    qreal m_masterVolume = 1.0;
};
