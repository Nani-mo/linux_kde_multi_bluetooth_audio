#include "pipewirecontroller.h"

#include <QMetaObject>
#include <QProcess>

#include <algorithm>
#include <string_view>

Q_DECLARE_METATYPE(PipeWireController::SinkInfo)

namespace
{
const struct pw_registry_events s_registryEvents = {
    .version = PW_VERSION_REGISTRY_EVENTS,
    .global = PipeWireController::registryGlobal,
    .global_remove = PipeWireController::registryGlobalRemove,
};

const struct pw_core_events s_coreEvents = {
    .version = PW_VERSION_CORE_EVENTS,
    .error = PipeWireController::coreError,
};

const struct pw_node_events s_nodeEvents = {
    .version = PW_VERSION_NODE_EVENTS,
    .info = PipeWireController::nodeInfo,
};
}

PipeWireController::PipeWireController(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<PipeWireController::SinkInfo>("PipeWireController::SinkInfo");
    pw_init(nullptr, nullptr);
}

PipeWireController::~PipeWireController()
{
    stop();
    pw_deinit();
}

bool PipeWireController::start()
{
    if (m_running) {
        return true;
    }

    m_loop = pw_thread_loop_new("multibtaudio-pw-loop", nullptr);
    if (!m_loop) {
        Q_EMIT connectionError(QStringLiteral("pw_thread_loop_new fehlgeschlagen"));
        return false;
    }

    m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    if (!m_context) {
        Q_EMIT connectionError(QStringLiteral("pw_context_new fehlgeschlagen"));
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
        return false;
    }

    if (pw_thread_loop_start(m_loop) != 0) {
        Q_EMIT connectionError(QStringLiteral("pw_thread_loop_start fehlgeschlagen"));
        pw_context_destroy(m_context);
        pw_thread_loop_destroy(m_loop);
        m_context = nullptr;
        m_loop = nullptr;
        return false;
    }

    pw_thread_loop_lock(m_loop);

    m_core = pw_context_connect(m_context, nullptr, 0);
    if (!m_core) {
        pw_thread_loop_unlock(m_loop);
        Q_EMIT connectionError(QStringLiteral("Verbindung zum PipeWire-Daemon fehlgeschlagen"));
        stop();
        return false;
    }

    pw_core_add_listener(m_core, &m_coreListener, &s_coreEvents, this);

    m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);
    if (!m_registry) {
        pw_thread_loop_unlock(m_loop);
        Q_EMIT connectionError(QStringLiteral("pw_core_get_registry fehlgeschlagen"));
        stop();
        return false;
    }

    pw_registry_add_listener(m_registry, &m_registryListener, &s_registryEvents, this);

    pw_thread_loop_unlock(m_loop);

    m_running = true;
    return true;
}

void PipeWireController::stop()
{
    if (m_loop) {
        pw_thread_loop_lock(m_loop);
        for (auto it = m_nodeBindings.constBegin(); it != m_nodeBindings.constEnd(); ++it) {
            NodeBinding *binding = it.value();
            spa_hook_remove(&binding->listener);
            pw_proxy_destroy(binding->proxy);
            delete binding;
        }
        m_nodeBindings.clear();
        if (m_registry) {
            spa_hook_remove(&m_registryListener);
            pw_proxy_destroy(reinterpret_cast<struct pw_proxy *>(m_registry));
            m_registry = nullptr;
        }
        if (m_core) {
            spa_hook_remove(&m_coreListener);
            pw_core_disconnect(m_core);
            m_core = nullptr;
        }
        pw_thread_loop_unlock(m_loop);
        pw_thread_loop_stop(m_loop);
    }

    if (m_context) {
        pw_context_destroy(m_context);
        m_context = nullptr;
    }

    if (m_loop) {
        pw_thread_loop_destroy(m_loop);
        m_loop = nullptr;
    }

    m_running = false;
}

bool PipeWireController::isRunning() const
{
    return m_running;
}

bool PipeWireController::setNodeVolume(quint32 id, qreal volume)
{
    // Direktes pw_node_set_param(SPA_PARAM_Props, channelVolumes) wird zwar
    // vom Node akzeptiert (per pw-cli enum-params verifiziert), hat aber
    // keinerlei sichtbaren Effekt: WirePlumber verwaltet die vom restlichen
    // Desktop wahrgenommene Lautstärke über einen eigenen, von der rohen
    // Node-Property entkoppelten Zustand. `wpctl set-volume` ist der
    // Weg, der tatsächlich mit dem restlichen System konsistent bleibt
    // (siehe SETUP.md).
    volume = std::clamp(volume, 0.0, 1.0);

    return QProcess::startDetached(QStringLiteral("wpctl"),
        {QStringLiteral("set-volume"), QString::number(id), QString::number(volume, 'f', 3)});
}

QByteArray PipeWireController::buildTestTonePcm()
{
    // Kurzer, perkussiver Klick (schnell abklingendes ~2kHz-Rechtecksignal,
    // sofort als klar unterscheidbarer Impuls hörbar) mehrfach mit Pausen
    // wiederholt - Format: Stereo, s16le, 48kHz, muss zu den in
    // playTestTone() übergebenen pw-cat-Argumenten passen.
    constexpr int kSampleRate = 48000;
    constexpr int kClickMs = 15;
    constexpr int kGapMs = 485;
    constexpr int kRepeats = 6;
    constexpr int kClickSamples = kSampleRate * kClickMs / 1000;
    constexpr int kGapSamples = kSampleRate * kGapMs / 1000;

    QByteArray pcm;
    pcm.reserve((kClickSamples + kGapSamples) * kRepeats * 4);

    for (int r = 0; r < kRepeats; ++r) {
        for (int i = 0; i < kClickSamples; ++i) {
            const double envelope = 1.0 - (static_cast<double>(i) / kClickSamples);
            const auto magnitude = static_cast<qint16>(envelope * 20000);
            const qint16 sample = ((i / 12) % 2 == 0) ? magnitude : static_cast<qint16>(-magnitude);
            pcm.append(reinterpret_cast<const char *>(&sample), sizeof(sample));
            pcm.append(reinterpret_cast<const char *>(&sample), sizeof(sample));
        }
        pcm.append(kGapSamples * 4, '\0');
    }
    return pcm;
}

bool PipeWireController::playTestTone()
{
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, process, &QObject::deleteLater);

    process->start(QStringLiteral("pw-cat"),
        {QStringLiteral("-p"), QStringLiteral("--raw"),
         QStringLiteral("--rate"), QStringLiteral("48000"),
         QStringLiteral("--channels"), QStringLiteral("2"),
         QStringLiteral("--format"), QStringLiteral("s16"),
         QStringLiteral("-")});

    if (!process->waitForStarted(500)) {
        process->deleteLater();
        return false;
    }

    process->write(buildTestTonePcm());
    process->closeWriteChannel();
    return true;
}

bool PipeWireController::isEligibleOutputSink(const struct spa_dict *props)
{
    if (!props) {
        return false;
    }

    const char *mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    if (!mediaClass || std::string_view(mediaClass) != "Audio/Sink") {
        return false;
    }

    // Eigene, selbst erzeugte virtuelle Sinks (Combine-Sink, Delay-Proxys -
    // siehe CombineSinkManager::kCombineSinkNodeName/delaySinkNodeName) sind
    // technisch ebenfalls Audio/Sink-Nodes, dürfen aber nicht als reguläre
    // Ausgabegeräte in der Liste auftauchen - sonst könnte ein Nutzer den
    // Kombi-Sink versehentlich als Ziel seiner eigenen Kombi-Ausgabe wählen.
    // Mit echten pw-dump-Daten verifiziert: diese Nodes haben kein
    // device.api gesetzt, daher reicht ein Namens-Präfix-Check als robuste,
    // von der jeweiligen Geräteart unabhängige Ausschlussregel.
    if (const char *nodeName = spa_dict_lookup(props, PW_KEY_NODE_NAME)) {
        if (std::string_view(nodeName).starts_with("multibtaudio_")) {
            return false;
        }
    }

    return true;
}

PipeWireController::SinkInfo PipeWireController::sinkInfoFromProps(quint32 id, const struct spa_dict *props)
{
    SinkInfo info;
    info.id = id;

    if (!props) {
        return info;
    }

    if (const char *nodeName = spa_dict_lookup(props, PW_KEY_NODE_NAME)) {
        info.name = QString::fromUtf8(nodeName);
    }
    if (const char *nodeDesc = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION)) {
        info.description = QString::fromUtf8(nodeDesc);
    }
    if (const char *btAddr = spa_dict_lookup(props, "api.bluez5.address")) {
        info.btAddress = QString::fromUtf8(btAddr);
    }
    if (const char *codec = spa_dict_lookup(props, "api.bluez5.codec")) {
        info.codec = QString::fromUtf8(codec);
    }
    if (const char *formFactor = spa_dict_lookup(props, PW_KEY_DEVICE_FORM_FACTOR)) {
        info.formFactor = QString::fromUtf8(formFactor);
    }

    return info;
}

void PipeWireController::registryGlobal(void *data, uint32_t id, uint32_t /*permissions*/,
                                         const char *type, uint32_t /*version*/,
                                         const struct spa_dict *props)
{
    auto *self = static_cast<PipeWireController *>(data);

    if (std::string_view(type) != PW_TYPE_INTERFACE_Node) {
        return;
    }

    // Vorfilter anhand der Registry-Properties (media.class ist hier bereits
    // verfügbar). Ob der Sink zählt (kein eigener virtueller Sink), klärt
    // sich erst nach dem Binden über den Node-info-Event (siehe handleNodeInfo).
    const char *mediaClass = props ? spa_dict_lookup(props, PW_KEY_MEDIA_CLASS) : nullptr;
    if (!mediaClass || std::string_view(mediaClass) != "Audio/Sink") {
        return;
    }

    const QString nodeName = QString::fromUtf8(spa_dict_lookup(props, PW_KEY_NODE_NAME));
    QMetaObject::invokeMethod(self, [self, id, nodeName]() {
        Q_EMIT self->audioSinkRegistered(id, nodeName);
    }, Qt::QueuedConnection);

    auto *binding = new NodeBinding;
    binding->self = self;
    binding->id = id;
    binding->proxy = static_cast<struct pw_proxy *>(
        pw_registry_bind(self->m_registry, id, type, PW_VERSION_NODE, 0));

    if (!binding->proxy) {
        delete binding;
        return;
    }

    pw_node_add_listener(reinterpret_cast<struct pw_node *>(binding->proxy),
                          &binding->listener, &s_nodeEvents, binding);

    self->m_nodeBindings.insert(id, binding);
}

void PipeWireController::registryGlobalRemove(void *data, uint32_t id)
{
    auto *self = static_cast<PipeWireController *>(data);

    NodeBinding *binding = self->m_nodeBindings.value(id, nullptr);
    const bool wasEligible = binding && binding->confirmedEligible;

    self->destroyBinding(id);

    if (wasEligible) {
        QMetaObject::invokeMethod(self, [self, id]() {
            Q_EMIT self->sinkRemoved(id);
        }, Qt::QueuedConnection);
    }
}

void PipeWireController::nodeInfo(void *data, const struct pw_node_info *info)
{
    auto *binding = static_cast<NodeBinding *>(data);
    binding->self->handleNodeInfo(binding, info);
}

void PipeWireController::handleNodeInfo(NodeBinding *binding, const struct pw_node_info *info)
{
    if (!info || !info->props || !isEligibleOutputSink(info->props)) {
        if (!binding->confirmedEligible) {
            destroyBinding(binding->id);
        }
        return;
    }

    const SinkInfo sinkInfo = sinkInfoFromProps(binding->id, info->props);
    const bool isNewSink = !binding->confirmedEligible;
    binding->confirmedEligible = true;

    QMetaObject::invokeMethod(this, [this, sinkInfo, isNewSink]() {
        if (isNewSink) {
            Q_EMIT sinkAdded(sinkInfo);
        } else {
            Q_EMIT sinkChanged(sinkInfo);
        }
    }, Qt::QueuedConnection);
}

void PipeWireController::destroyBinding(quint32 id)
{
    NodeBinding *binding = m_nodeBindings.take(id);
    if (!binding) {
        return;
    }

    spa_hook_remove(&binding->listener);
    pw_proxy_destroy(binding->proxy);
    delete binding;
}

void PipeWireController::coreError(void *data, uint32_t id, int seq, int res, const char *message)
{
    auto *self = static_cast<PipeWireController *>(data);
    const QString msg = QStringLiteral("PipeWire-Fehler (id=%1, seq=%2, res=%3): %4")
                             .arg(id).arg(seq).arg(res).arg(QString::fromUtf8(message));

    QMetaObject::invokeMethod(self, [self, msg]() {
        Q_EMIT self->connectionError(msg);
    }, Qt::QueuedConnection);
}
