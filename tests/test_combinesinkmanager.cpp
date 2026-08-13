#include "combinesinkmanager.h"
#include "pipewirecontroller.h"

#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

namespace
{
/**
 * Liest `default.configured.audio.sink` aus den PipeWire-Metadaten - den
 * Key, an dem Plasmas Lautstärke-Applet ablesen will, welches Gerät
 * ausgewählt ist (siehe SETUP.md „KDE-Sound-Menü verbuggt").
 */
QString configuredDefaultSinkName()
{
    QProcess process;
    process.start(QStringLiteral("pw-metadata"),
        {QStringLiteral("-n"), QStringLiteral("default"), QStringLiteral("0"),
         QStringLiteral("default.configured.audio.sink")});
    if (!process.waitForFinished(2000)) {
        return {};
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    static const QRegularExpression namePattern(QStringLiteral("\"name\"\\s*:\\s*\"([^\"]+)\""));
    const QRegularExpressionMatch match = namePattern.match(output);
    return match.hasMatch() ? match.captured(1) : QString();
}
}

// Läuft gegen einen echten laufenden PipeWire-Daemon (siehe PLAN.md
// Abschnitt 6: "gegen echten pipewire-Test-Daemon"). CombineSinkManager
// hält einen dauerhaften combine-stream-Sink plus ein unabhängiges
// module-loopback pro Zielgerät (siehe PLAN.md Phase 7.2, SETUP.md
// „Checkbox-Toggle stoppt überall"). Ziel-Node-Namen müssen nicht real
// existieren: die Module laden trotzdem erfolgreich, die Loopback-Ziele
// bleiben dann nur unverlinkt.
class TestCombineSinkManager : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void failsWithoutAttachedController();
    void loadsAndUnloadsModuleAgainstRealDaemon();
    void loadsDelayLoopbackAgainstRealDaemon();
    void addingSecondTargetKeepsFirstActive();
    void fullCycleLeavesNoStaleDefaultSink();
};

void TestCombineSinkManager::failsWithoutAttachedController()
{
    CombineSinkManager manager;

    CombineSinkManager::Target target;
    target.sink.id = 1;
    target.sink.name = QStringLiteral("irrelevant");

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

    CombineSinkManager::Target target;
    target.sink.id = 999999;
    target.sink.name = QStringLiteral("nonexistent_test_sink_for_ctest");

    QSignalSpy changedSpy(&manager, &CombineSinkManager::activeTargetsChanged);
    QVERIFY(manager.setActiveTargets({target}));
    QVERIFY(manager.isActive());
    QCOMPARE(manager.activeTargetIds(), QSet<quint32>({target.sink.id}));
    QCOMPARE(changedSpy.count(), 1);

    QVERIFY(manager.setActiveTargets({}));
    QVERIFY(!manager.isActive());
    QCOMPARE(changedSpy.count(), 2);
}

void TestCombineSinkManager::loadsDelayLoopbackAgainstRealDaemon()
{
    // Verifiziert, dass für delayMs != 0 das per-Gerät-Loopback mit
    // target.delay.sec geladen werden kann (siehe PLAN.md Phase 7.1/7.2,
    // SETUP.md „Delay-Ergebnis") - der Ziel-Node muss auch hier nicht real
    // existieren, das Modul lädt trotzdem.
    PipeWireController controller;
    QVERIFY(controller.start());

    CombineSinkManager manager;
    manager.attachController(&controller);

    CombineSinkManager::Target target;
    target.sink.id = 999998;
    target.sink.name = QStringLiteral("nonexistent_test_sink_for_ctest_delay");
    target.delayMs = 120;

    QSignalSpy changedSpy(&manager, &CombineSinkManager::activeTargetsChanged);
    QVERIFY(manager.setActiveTargets({target}));
    QVERIFY(manager.isActive());
    QCOMPARE(manager.activeTargetIds(), QSet<quint32>({target.sink.id}));
    QCOMPARE(changedSpy.count(), 1);

    QVERIFY(manager.setActiveTargets({}));
    QVERIFY(!manager.isActive());
    QCOMPARE(changedSpy.count(), 2);
}

void TestCombineSinkManager::addingSecondTargetKeepsFirstActive()
{
    // Regressionstest für den vom Nutzer im Live-Test gefundenen Bug (siehe
    // SETUP.md „Checkbox-Toggle stoppt überall"): Ein zweites Gerät
    // hinzuzufügen darf den Combine-Sink nicht neu erzeugen und das erste
    // Gerät nicht anfassen. Kann von außen nicht direkt an der Sink-
    // Objektidentität geprüft werden, aber zumindest black-box: die
    // gesamte Auswahl-Sequenz (0->1->2->1->0 Geräte) muss ohne Fehler
    // durchlaufen und activeTargetIds() muss an jedem Schritt exakt der
    // erwarteten Auswahl entsprechen.
    PipeWireController controller;
    QVERIFY(controller.start());

    CombineSinkManager manager;
    manager.attachController(&controller);

    CombineSinkManager::Target first;
    first.sink.id = 999997;
    first.sink.name = QStringLiteral("nonexistent_test_sink_for_ctest_a");

    CombineSinkManager::Target second;
    second.sink.id = 999996;
    second.sink.name = QStringLiteral("nonexistent_test_sink_for_ctest_b");
    second.delayMs = 50;

    QVERIFY(manager.setActiveTargets({first}));
    QCOMPARE(manager.activeTargetIds(), QSet<quint32>({first.sink.id}));

    QVERIFY(manager.setActiveTargets({first, second}));
    QCOMPARE(manager.activeTargetIds(), QSet<quint32>({first.sink.id, second.sink.id}));

    QVERIFY(manager.setActiveTargets({first}));
    QCOMPARE(manager.activeTargetIds(), QSet<quint32>({first.sink.id}));

    QVERIFY(manager.setActiveTargets({}));
    QVERIFY(!manager.isActive());
}

void TestCombineSinkManager::fullCycleLeavesNoStaleDefaultSink()
{
    // Regressionstest für den vom Nutzer gemeldeten Bug „KDE-Sound-Menü
    // verbuggt" (siehe SETUP.md): Nach einem vollständigen Ein/Aus-Zyklus
    // darf `default.configured.audio.sink` nicht mehr auf den inzwischen
    // zerstörten Combine-Sink zeigen - sonst findet Plasmas Applet das
    // konfigurierte Gerät nicht in seiner Liste und zeigt gar keine Auswahl
    // mehr an. Läuft gegen den echten Daemon; der Zustand vor dem Test wird
    // durch den Fix selbst wiederhergestellt (genau das wird hier geprüft).
    //
    // Bekannte Einschränkung: `kCombineSinkNodeName` ist ein fester Name
    // ("multibtaudio_combine") - läuft das echte Plasmoid währenddessen
    // *ebenfalls* mit aktiver Kombi-Ausgabe, kollidiert dieser Test mit
    // dessen Sink (gleicher Name, zwei Nodes) und schlägt fälschlich fehl
    // (siehe SETUP.md). Vor dem Ausführen ggf. prüfen: `wpctl status |
    // grep multibtaudio_combine` sollte leer sein.
    const QString beforeCycle = configuredDefaultSinkName();

    PipeWireController controller;
    QVERIFY(controller.start());

    CombineSinkManager manager;
    manager.attachController(&controller);

    CombineSinkManager::Target target;
    target.sink.id = 999995;
    target.sink.name = QStringLiteral("nonexistent_test_sink_for_ctest_stale");

    QVERIFY(manager.setActiveTargets({target}));
    // WirePlumber Zeit geben, den neu erschienenen Sink zu verarbeiten (es
    // schaltet von sich aus auf ihn um, siehe SETUP.md) - sonst prüft der
    // Test den Aufräumpfad, ohne dass je etwas aufzuräumen war.
    QTest::qWait(1500);

    QVERIFY(manager.setActiveTargets({}));
    QTest::qWait(1000);

    const QString afterCycle = configuredDefaultSinkName();
    QVERIFY2(!afterCycle.contains(QLatin1String("multibtaudio")),
             qPrintable(QStringLiteral("default.configured.audio.sink zeigt nach dem Zyklus "
                                       "auf den zerstörten Combine-Sink: '%1' (vorher: '%2')")
                            .arg(afterCycle, beforeCycle)));
}

QTEST_GUILESS_MAIN(TestCombineSinkManager)
#include "test_combinesinkmanager.moc"
