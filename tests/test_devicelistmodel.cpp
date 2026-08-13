#include "devicelistmodel.h"
#include "pipewirecontroller.h"

#include <QSignalSpy>
#include <QTest>

// Testet DeviceListModel isoliert von der echten PipeWire-Verbindung, indem
// die PipeWireController-Signale direkt simuliert werden ("Mock der
// PipeWire-Events", siehe PLAN.md Phase 1). controller.start() wird
// absichtlich nie aufgerufen.
class TestDeviceListModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void addsRowOnSinkAdded();
    void removesRowOnSinkRemoved();
    void updatesRowOnSinkChanged();
    void ignoresDuplicateSinkAdded();
    void setDataUpdatesActiveRole();
    void toolEnabledGatesActiveCount();
    void toolEnabledIsInvokableFromQml();
    void masterVolumeIsInvokableFromQmlAndClamped();
};

void TestDeviceListModel::addsRowOnSinkAdded()
{
    PipeWireController controller;
    DeviceListModel model;
    model.setController(&controller);

    QCOMPARE(model.rowCount(), 0);

    PipeWireController::SinkInfo sink;
    sink.id = 42;
    sink.name = QStringLiteral("bluez_output.AA_BB_CC_DD_EE_FF.1");
    sink.description = QStringLiteral("Kopfhörer");

    QSignalSpy insertedSpy(&model, &QAbstractItemModel::rowsInserted);
    controller.sinkAdded(sink);

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(insertedSpy.count(), 1);

    const QModelIndex idx = model.index(0, 0);
    QCOMPARE(model.data(idx, DeviceListModel::IdRole).toUInt(), 42u);
    QCOMPARE(model.data(idx, DeviceListModel::NameRole).toString(), sink.name);
    QCOMPARE(model.data(idx, DeviceListModel::DescriptionRole).toString(), sink.description);
    QCOMPARE(model.data(idx, DeviceListModel::ActiveRole).toBool(), false);
}

void TestDeviceListModel::removesRowOnSinkRemoved()
{
    PipeWireController controller;
    DeviceListModel model;
    model.setController(&controller);

    PipeWireController::SinkInfo sink;
    sink.id = 7;
    sink.name = QStringLiteral("Lautsprecher");
    controller.sinkAdded(sink);
    QCOMPARE(model.rowCount(), 1);

    QSignalSpy removedSpy(&model, &QAbstractItemModel::rowsRemoved);
    controller.sinkRemoved(7);

    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(removedSpy.count(), 1);
}

void TestDeviceListModel::updatesRowOnSinkChanged()
{
    PipeWireController controller;
    DeviceListModel model;
    model.setController(&controller);

    PipeWireController::SinkInfo sink;
    sink.id = 1;
    sink.name = QStringLiteral("Alt");
    controller.sinkAdded(sink);

    sink.name = QStringLiteral("Neu");
    QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
    controller.sinkChanged(sink);

    QCOMPARE(changedSpy.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), DeviceListModel::NameRole).toString(), QStringLiteral("Neu"));
}

void TestDeviceListModel::ignoresDuplicateSinkAdded()
{
    PipeWireController controller;
    DeviceListModel model;
    model.setController(&controller);

    PipeWireController::SinkInfo sink;
    sink.id = 5;
    controller.sinkAdded(sink);
    controller.sinkAdded(sink);

    QCOMPARE(model.rowCount(), 1);
}

void TestDeviceListModel::setDataUpdatesActiveRole()
{
    PipeWireController controller;
    DeviceListModel model;
    model.setController(&controller);

    PipeWireController::SinkInfo sink;
    sink.id = 9;
    controller.sinkAdded(sink);

    const QModelIndex idx = model.index(0, 0);
    QVERIFY(model.setData(idx, true, DeviceListModel::ActiveRole));
    QCOMPARE(model.data(idx, DeviceListModel::ActiveRole).toBool(), true);

    QVERIFY(model.setData(idx, 0.5, DeviceListModel::VolumeRole));
    QCOMPARE(model.data(idx, DeviceListModel::VolumeRole).toDouble(), 0.5);
}

void TestDeviceListModel::toolEnabledGatesActiveCount()
{
    // Der zentrale Ein/Aus-Schalter (PLAN.md Phase 7.2) soll die Kombi-
    // Ausgabe pausieren, ohne die Geräteauswahl zu verwerfen - activeCount
    // (treibt u.a. das Tray-Icon-Badge) muss daher bei toolEnabled=false auf
    // 0 fallen, obwohl das Gerät weiterhin als "active" markiert bleibt.
    PipeWireController controller;
    DeviceListModel model;
    model.setController(&controller);

    QVERIFY(model.toolEnabled());

    PipeWireController::SinkInfo sink;
    sink.id = 3;
    controller.sinkAdded(sink);

    QVERIFY(model.setActive(0, true));
    QCOMPARE(model.activeCount(), 1);

    QSignalSpy toolEnabledSpy(&model, &DeviceListModel::toolEnabledChanged);
    model.setToolEnabled(false);
    QVERIFY(!model.toolEnabled());
    QCOMPARE(toolEnabledSpy.count(), 1);
    QCOMPARE(model.activeCount(), 0);
    QCOMPARE(model.data(model.index(0, 0), DeviceListModel::ActiveRole).toBool(), true);

    model.setToolEnabled(true);
    QCOMPARE(model.activeCount(), 1);
}

void TestDeviceListModel::toolEnabledIsInvokableFromQml()
{
    // Regressionstest für einen echten, per Nutzer-Live-Test gefundenen Bug
    // (siehe SETUP.md): setToolEnabled() diente zunächst nur als
    // Q_PROPERTY-WRITE-Methode ohne Q_INVOKABLE - ein direkter C++-Aufruf
    // (wie in toolEnabledGatesActiveCount oben) hätte das nie aufgedeckt,
    // weil er den Meta-Objekt-Aufrufpfad umgeht, über den main.qmls
    // "deviceModel.setToolEnabled(checked)" tatsächlich läuft.
    // QMetaObject::invokeMethod-nach-Name simuliert genau diesen Pfad.
    DeviceListModel model;
    QVERIFY(model.toolEnabled());

    QVERIFY(QMetaObject::invokeMethod(&model, "setToolEnabled", Q_ARG(bool, false)));
    QVERIFY(!model.toolEnabled());
}

void TestDeviceListModel::masterVolumeIsInvokableFromQmlAndClamped()
{
    // Gleiche Lektion wie bei toolEnabledIsInvokableFromQml oben angewendet:
    // Aufruf über QMetaObject::invokeMethod-nach-Name statt Direktaufruf,
    // damit ein fehlendes Q_INVOKABLE hier von Anfang an auffallen würde.
    DeviceListModel model;
    QCOMPARE(model.masterVolume(), 1.0);

    QSignalSpy volumeSpy(&model, &DeviceListModel::masterVolumeChanged);
    QVERIFY(QMetaObject::invokeMethod(&model, "setMasterVolume", Q_ARG(qreal, 0.4)));
    QCOMPARE(model.masterVolume(), 0.4);
    QCOMPARE(volumeSpy.count(), 1);

    // Werte außerhalb 0..1 werden geklemmt, nicht einfach übernommen.
    model.setMasterVolume(2.0);
    QCOMPARE(model.masterVolume(), 1.0);
    model.setMasterVolume(-0.5);
    QCOMPARE(model.masterVolume(), 0.0);
}

QTEST_GUILESS_MAIN(TestDeviceListModel)
#include "test_devicelistmodel.moc"
