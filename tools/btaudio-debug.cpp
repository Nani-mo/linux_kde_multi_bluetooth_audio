#include "pipewirecontroller.h"

#include <QCoreApplication>
#include <QTextStream>

// Konsolen-Testtool: verifiziert die Backend-Geräteerkennung isoliert von
// jeglicher QML-/Plasmoid-UI, siehe PLAN.md Phase 1. Loggt jede erkannte
// Bluetooth-Audio-Sink-Änderung, bis das Programm mit Ctrl+C beendet wird.

namespace
{
QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    PipeWireController controller;

    QObject::connect(&controller, &PipeWireController::sinkAdded, [](const PipeWireController::SinkInfo &sink) {
        out() << "+ Sink hinzugefügt: id=" << sink.id
              << " name=" << sink.name
              << " description=" << sink.description
              << " formFactor=" << sink.formFactor
              << " btAddress=" << sink.btAddress
              << " codec=" << sink.codec
              << Qt::endl;
    });

    QObject::connect(&controller, &PipeWireController::sinkRemoved, [](quint32 id) {
        out() << "- Sink entfernt: id=" << id << Qt::endl;
    });

    QObject::connect(&controller, &PipeWireController::sinkChanged, [](const PipeWireController::SinkInfo &sink) {
        out() << "~ Sink geändert: id=" << sink.id << " name=" << sink.name << Qt::endl;
    });

    QObject::connect(&controller, &PipeWireController::connectionError, [](const QString &message) {
        out() << "! PipeWire-Fehler: " << message << Qt::endl;
    });

    if (!controller.start()) {
        out() << "Verbindung zu PipeWire konnte nicht hergestellt werden." << Qt::endl;
        return 1;
    }

    out() << "btaudio-debug läuft. Warte auf Bluetooth-Audio-Sinks (Ctrl+C zum Beenden) ..." << Qt::endl;

    return app.exec();
}
