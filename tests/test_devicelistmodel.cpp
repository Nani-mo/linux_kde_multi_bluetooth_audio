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

QTEST_GUILESS_MAIN(TestDeviceListModel)
#include "test_devicelistmodel.moc"
