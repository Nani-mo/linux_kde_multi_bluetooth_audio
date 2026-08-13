#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

#include <KSharedConfig>

/**
 * Persistiert die zuletzt genutzte Geräte-Kombination samt Lautstärke-/
 * Delay-Werten über KConfig, damit sie bei erneutem Verbinden automatisch
 * vorgeschlagen werden kann. Geräte werden per opakem `deviceKey`
 * identifiziert - der Aufrufer (DeviceListModel) entscheidet, was das ist
 * (Bluetooth-MAC, wenn vorhanden, sonst PipeWire-Node-Name als Fallback für
 * Nicht-Bluetooth-Geräte, siehe PLAN.md Phase 7.2); ConfigStore selbst
 * behandelt es nur als Gruppennamen-Bestandteil.
 * Siehe PLAN.md Abschnitt 3.5.
 */
class ConfigStore : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    struct DeviceSettings {
        bool active = false;
        qreal volume = 1.0;
        int delayMs = 0;
    };

    explicit ConfigStore(QObject *parent = nullptr);

    DeviceSettings settingsForDevice(const QString &deviceKey) const;
    void setSettingsForDevice(const QString &deviceKey, const DeviceSettings &settings);

    /** Globaler Ein/Aus-Zustand des zentralen Schalters (PLAN.md Phase 7.2), unabhängig von der Geräteauswahl. */
    bool toolEnabled() const;
    void setToolEnabled(bool enabled);

    /** Zentrale Master-Lautstärke (PLAN.md Phase 7.2), unabhängig von den Pro-Gerät-Lautstärken. */
    qreal masterVolume() const;
    void setMasterVolume(qreal volume);

    void sync();

private:
    KSharedConfig::Ptr m_config;
};
