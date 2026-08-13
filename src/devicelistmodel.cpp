#include "devicelistmodel.h"

#include <algorithm>
#include <utility>

namespace
{
// Persistenz-Schlüssel für ConfigStore: per Bluetooth-MAC, falls vorhanden
// (stabil über Reconnects hinweg) - sonst per PipeWire-Node-Name als
// Fallback für Nicht-Bluetooth-Geräte (z.B. eingebaute Lautsprecher, USB-
// Audio), die keine MAC-Adresse haben, aber einen für die jeweilige
// Hardware stabilen Node-Namen (siehe PLAN.md Phase 7.2 "Alle
// ausgabefähigen Audiogeräte").
QString persistenceKey(const PipeWireController::SinkInfo &sink)
{
    return sink.btAddress.isEmpty() ? sink.name : sink.btAddress;
}
}

DeviceListModel::DeviceListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void DeviceListModel::setController(PipeWireController *controller)
{
    if (m_controller == controller) {
        return;
    }

    if (m_controller) {
        disconnect(m_controller, nullptr, this, nullptr);
    }

    m_controller = controller;

    if (m_controller) {
        connect(m_controller, &PipeWireController::sinkAdded, this, &DeviceListModel::onSinkAdded);
        connect(m_controller, &PipeWireController::sinkRemoved, this, &DeviceListModel::onSinkRemoved);
        connect(m_controller, &PipeWireController::sinkChanged, this, &DeviceListModel::onSinkChanged);
    }
}

void DeviceListModel::setCombineSinkManager(CombineSinkManager *manager)
{
    m_combineSinkManager = manager;
}

void DeviceListModel::setConfigStore(ConfigStore *store)
{
    m_configStore = store;

    if (m_configStore) {
        const bool storedEnabled = m_configStore->toolEnabled();
        if (storedEnabled != m_toolEnabled) {
            m_toolEnabled = storedEnabled;
            Q_EMIT toolEnabledChanged();
        }

        const qreal storedVolume = m_configStore->masterVolume();
        if (!qFuzzyCompare(storedVolume, m_masterVolume)) {
            m_masterVolume = storedVolume;
            Q_EMIT masterVolumeChanged();
        }
    }
}

void DeviceListModel::setToolEnabled(bool enabled)
{
    if (m_toolEnabled == enabled) {
        return;
    }

    m_toolEnabled = enabled;
    Q_EMIT toolEnabledChanged();

    if (m_configStore) {
        m_configStore->setToolEnabled(enabled);
        m_configStore->sync();
    }

    updateCombineTargets();
}

void DeviceListModel::setMasterVolume(qreal volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(m_masterVolume, volume)) {
        return;
    }

    m_masterVolume = volume;
    Q_EMIT masterVolumeChanged();

    if (m_configStore) {
        m_configStore->setMasterVolume(volume);
        m_configStore->sync();
    }

    applyEffectiveVolumesForActiveEntries();
}

bool DeviceListModel::setActive(int row, bool active)
{
    return setData(index(row), active, ActiveRole);
}

bool DeviceListModel::setVolume(int row, qreal volume)
{
    return setData(index(row), volume, VolumeRole);
}

bool DeviceListModel::setDelayMs(int row, int delayMs)
{
    return setData(index(row), delayMs, DelayMsRole);
}

int DeviceListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_entries.size();
}

QVariant DeviceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }

    const Entry &entry = m_entries.at(index.row());

    switch (role) {
    case IdRole:
        return entry.sink.id;
    case NameRole:
        return entry.sink.name;
    case DescriptionRole:
        return entry.sink.description;
    case FormFactorRole:
        return entry.sink.formFactor;
    case BtAddressRole:
        return entry.sink.btAddress;
    case CodecRole:
        return entry.sink.codec;
    case ActiveRole:
        return entry.active;
    case VolumeRole:
        return entry.volume;
    case DelayMsRole:
        return entry.delayMs;
    default:
        return {};
    }
}

bool DeviceListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return false;
    }

    Entry &entry = m_entries[index.row()];

    switch (role) {
    case ActiveRole:
        entry.active = value.toBool();
        break;
    case VolumeRole:
        entry.volume = value.toReal();
        break;
    case DelayMsRole:
        entry.delayMs = value.toInt();
        break;
    default:
        return false;
    }

    Q_EMIT dataChanged(index, index, {role});
    persistEntry(entry);

    if (role == ActiveRole || role == DelayMsRole) {
        updateCombineTargets();
    } else if (role == VolumeRole) {
        applyEffectiveVolume(entry);
    }

    return true;
}

QHash<int, QByteArray> DeviceListModel::roleNames() const
{
    return {
        {IdRole, "deviceId"},
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {FormFactorRole, "formFactor"},
        {BtAddressRole, "btAddress"},
        {CodecRole, "codec"},
        {ActiveRole, "active"},
        {VolumeRole, "volume"},
        {DelayMsRole, "delayMs"},
    };
}

int DeviceListModel::indexForId(quint32 id) const
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).sink.id == id) {
            return i;
        }
    }
    return -1;
}

void DeviceListModel::onSinkAdded(const PipeWireController::SinkInfo &sink)
{
    if (indexForId(sink.id) >= 0) {
        return;
    }

    Entry entry;
    entry.sink = sink;

    const QString key = persistenceKey(sink);
    if (m_configStore && !key.isEmpty()) {
        const ConfigStore::DeviceSettings settings = m_configStore->settingsForDevice(key);
        entry.active = settings.active;
        entry.volume = settings.volume;
        entry.delayMs = settings.delayMs;
    }

    const int row = m_entries.size();
    beginInsertRows(QModelIndex(), row, row);
    m_entries.append(entry);
    endInsertRows();

    if (entry.active) {
        updateCombineTargets();
    }
    applyEffectiveVolume(entry);
}

void DeviceListModel::onSinkRemoved(quint32 id)
{
    const int row = indexForId(id);
    if (row < 0) {
        return;
    }

    const bool wasActive = m_entries.at(row).active;

    beginRemoveRows(QModelIndex(), row, row);
    m_entries.removeAt(row);
    endRemoveRows();

    if (wasActive) {
        updateCombineTargets();
    }
}

void DeviceListModel::updateCombineTargets()
{
    QVector<CombineSinkManager::Target> activeSinks;
    if (m_toolEnabled) {
        for (const Entry &entry : std::as_const(m_entries)) {
            if (entry.active) {
                activeSinks.append({entry.sink, entry.delayMs});
            }
        }
    }

    if (activeSinks.size() != m_activeCount) {
        m_activeCount = activeSinks.size();
        Q_EMIT activeCountChanged();
    }

    if (m_combineSinkManager) {
        m_combineSinkManager->setActiveTargets(activeSinks);
    }
}

void DeviceListModel::applyEffectiveVolume(const Entry &entry)
{
    if (!m_controller) {
        return;
    }
    // Effektive Lautstärke = Pro-Gerät-Regler * zentraler Regler,
    // angewendet auf die reale Geräte-Node - siehe Erklärung im Header zu
    // masterVolume(), warum das (und nicht die Lautstärke des Combine-
    // Sinks selbst) der Weg ist, der tatsächlich bei allen Apps wirkt.
    m_controller->setNodeVolume(entry.sink.id, entry.volume * m_masterVolume);
}

void DeviceListModel::applyEffectiveVolumesForActiveEntries()
{
    for (const Entry &entry : std::as_const(m_entries)) {
        if (entry.active) {
            applyEffectiveVolume(entry);
        }
    }
}

void DeviceListModel::persistEntry(const Entry &entry)
{
    const QString key = persistenceKey(entry.sink);
    if (!m_configStore || key.isEmpty()) {
        return;
    }

    ConfigStore::DeviceSettings settings;
    settings.active = entry.active;
    settings.volume = entry.volume;
    settings.delayMs = entry.delayMs;

    m_configStore->setSettingsForDevice(key, settings);
    m_configStore->sync();
}

void DeviceListModel::onSinkChanged(const PipeWireController::SinkInfo &sink)
{
    const int row = indexForId(sink.id);
    if (row < 0) {
        return;
    }

    m_entries[row].sink = sink;
    const QModelIndex idx = index(row);
    Q_EMIT dataChanged(idx, idx);
}
