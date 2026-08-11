#include "combinesinkmanager.h"
#include "pipewirecontroller.h"

#include <QSignalSpy>
#include <QTest>

// Läuft gegen einen echten laufenden PipeWire-Daemon (siehe PLAN.md
// Abschnitt 6: "gegen echten pipewire-Test-Daemon") - CombineSinkManager
// lädt module-combine-stream in den lokalen Client-Context von
// PipeWireController (siehe Phase-0-Spike-Ergebnis in SETUP.md). Der
// Ziel-Node-Name muss nicht real existieren: das Modul lädt trotzdem
// erfolgreich, es entstehen nur keine Streams (kein match in stream.rules).
class TestCombineSinkManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void failsWithoutAttachedController();
    void loadsAndUnloadsModuleAgainstRealDaemon();
};

void TestCombineSinkManager::failsWithoutAttachedController()
{
    CombineSinkManager manager;

    PipeWireController::SinkInfo target;
    target.id = 1;
    target.name = QStringLiteral("irrelevant");

    QSignalSpy errorSpy(&manager, &CombineSinkManager::errorOccurred);
    QVERIFY(!manager.setActiveTargets({target}));
    QCOMPARE(errorSpy.count(), 1);
    QVERIFY(!manager.isActive());
}

void TestCombineSinkManager::loadsAndUnloadsModuleAgainstRealDaemon()
{
    PipeWireController controller;
    QVERIFY(controller.start());

    CombineSinkManager manager;
    manager.attachController(&controller);

    PipeWireController::SinkInfo target;
    target.id = 999999;
    target.name = QStringLiteral("nonexistent_test_sink_for_ctest");

    QSignalSpy changedSpy(&manager, &CombineSinkManager::activeTargetsChanged);
    QVERIFY(manager.setActiveTargets({target}));
    QVERIFY(manager.isActive());
    QCOMPARE(manager.activeTargetIds(), QSet<quint32>({target.id}));
    QCOMPARE(changedSpy.count(), 1);

    QVERIFY(manager.setActiveTargets({}));
    QVERIFY(!manager.isActive());
    QCOMPARE(changedSpy.count(), 2);
}

QTEST_GUILESS_MAIN(TestCombineSinkManager)
#include "test_combinesinkmanager.moc"
