#include "configstore.h"

#include <KConfigGroup>

namespace
{
QString groupNameForDevice(const QString &deviceKey)
{
    return QStringLiteral("Device-%1").arg(deviceKey);
}
}

ConfigStore::ConfigStore(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("plasma-multi-bt-audiorc")))
{
}

ConfigStore::DeviceSettings ConfigStore::settingsForDevice(const QString &deviceKey) const
{
    const KConfigGroup group = m_config->group(groupNameForDevice(deviceKey));

    DeviceSettings settings;
    settings.active = group.readEntry("Active", false);
    settings.volume = group.readEntry("Volume", 1.0);
    settings.delayMs = group.readEntry("DelayMs", 0);
    return settings;
}

void ConfigStore::setSettingsForDevice(const QString &deviceKey, const DeviceSettings &settings)
{
    KConfigGroup group = m_config->group(groupNameForDevice(deviceKey));
    group.writeEntry("Active", settings.active);
    group.writeEntry("Volume", settings.volume);
    group.writeEntry("DelayMs", settings.delayMs);
}

bool ConfigStore::toolEnabled() const
{
    const KConfigGroup group = m_config->group(QStringLiteral("General"));
    return group.readEntry("ToolEnabled", true);
}

void ConfigStore::setToolEnabled(bool enabled)
{
    KConfigGroup group = m_config->group(QStringLiteral("General"));
    group.writeEntry("ToolEnabled", enabled);
}

qreal ConfigStore::masterVolume() const
{
    const KConfigGroup group = m_config->group(QStringLiteral("General"));
    return group.readEntry("MasterVolume", 1.0);
}

void ConfigStore::setMasterVolume(qreal volume)
{
    KConfigGroup group = m_config->group(QStringLiteral("General"));
    group.writeEntry("MasterVolume", volume);
}

void ConfigStore::sync()
{
    m_config->sync();
}
