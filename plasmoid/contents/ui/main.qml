import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.extras as PlasmaExtras
import org.kde.kirigami as Kirigami
import org.kde.private.multibtaudio as MultiBtAudio

// Popup-Hauptansicht. Verdrahtet PipeWireController -> DeviceListModel ->
// CombineSinkManager/ConfigStore (siehe PLAN.md Abschnitt 2/3/3.5). Die
// Checkbox-Auswahl in der Geräteliste steuert direkt die reale
// Kombi-Ausgabe und der Lautstärke-Regler die reale Gerätelautstärke
// (über wpctl, siehe PipeWireController::setNodeVolume) - beides wird
// über ConfigStore persistiert. Der Delay-Regler wirkt über
// CombineSinkManager auf einen echten module-loopback-Proxy pro Gerät
// (target.delay.sec), siehe PLAN.md Phase 7.1 / SETUP.md „Delay-Ergebnis
// Teil 2". Zeigt seit Phase 7.2 alle Audio-Ausgabegeräte, nicht mehr nur
// Bluetooth (PipeWireController::isEligibleOutputSink).
PlasmoidItem {
    id: root

    MultiBtAudio.PipeWireController {
        id: pwController

        Component.onCompleted: {
            if (!start()) {
                console.warn("Multi-BT-Audio: PipeWire-Verbindung fehlgeschlagen");
            }
        }
        onConnectionError: (message) => console.warn("Multi-BT-Audio (PipeWire):", message)
    }

    MultiBtAudio.CombineSinkManager {
        id: combineManager

        Component.onCompleted: attachController(pwController)
        onErrorOccurred: (message) => console.warn("Multi-BT-Audio (CombineSinkManager):", message)
    }

    MultiBtAudio.ConfigStore {
        id: configStore
    }

    MultiBtAudio.DeviceListModel {
        id: deviceModel

        Component.onCompleted: {
            setController(pwController);
            setCombineSinkManager(combineManager);
            setConfigStore(configStore);
        }
    }

    compactRepresentation: CompactRepresentation {
        plasmoidItem: root
        activeCount: deviceModel.activeCount
    }

    fullRepresentation: PlasmaExtras.Representation {
        // Layout.preferredWidth/Height statt implicitWidth/Height hätte
        // hier keinen Effekt: der Popup-Container ist kein Layout, das
        // Representation-Root braucht daher eine eigene implizite Größe,
        // sonst bleibt das Popup 0x0 (unsichtbar) - genau das Problem, das
        // beim ersten echten Panel-Test auftrat (kein Popup beim Klick).
        implicitWidth: Kirigami.Units.gridUnit * 24
        implicitHeight: Kirigami.Units.gridUnit * 16

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing

            // Zentraler Ein/Aus-Schalter (PLAN.md Phase 7.2, Nutzerwunsch):
            // baut die Kombi-Ausgabe komplett ab/wieder auf, ohne die
            // Geräteauswahl zu verwerfen (siehe DeviceListModel::toolEnabled).
            // Bewusst oben platziert, damit er sofort sichtbar ist.
            RowLayout {
                Layout.fillWidth: true

                QQC2.Label {
                    Layout.fillWidth: true
                    // Zustand zusätzlich als Text, nicht nur über die
                    // Switch-Färbung: die reicht je nach Farbschema/Akzent-
                    // farbe für einen eindeutigen An/Aus-Eindruck nicht aus
                    // (Nutzer-Feedback: "beide sind einfach nur grau").
                    text: deviceModel.toolEnabled
                        ? i18n("Multi-Audio-Output: aktiv")
                        : i18n("Multi-Audio-Output: deaktiviert")
                    font.bold: true
                }

                QQC2.Switch {
                    checked: deviceModel.toolEnabled
                    onToggled: deviceModel.setToolEnabled(checked)
                }
            }

            // Zentraler Lautstärkeregler (PLAN.md Phase 7.2, Nutzerwunsch):
            // multipliziert sich mit der Lautstärke jedes aktiven Geräts
            // (DeviceListModel::setMasterVolume), zusätzlich zu den
            // Pro-Gerät-Reglern weiter unten - nicht (wie ein erster Versuch)
            // über die Lautstärke des gemeinsamen Combine-Sinks selbst, die
            // von den meisten Apps (Spotify, Browser - alles, was über die
            // PulseAudio-Kompatibilitätsschicht läuft) schlicht ignoriert
            // wird, siehe DeviceListModel::masterVolume() und SETUP.md
            // „Zentraler Lautstärkeregler wirkungslos". Throttle-statt-
            // Debounce-Muster wie beim Pro-Gerät-Regler in DeviceItem.qml
            // übernommen - ein reiner Debounce ließ dort kleine Bewegungen
            // "tot" wirken, siehe SETUP.md.
            RowLayout {
                Layout.fillWidth: true
                enabled: deviceModel.toolEnabled

                Kirigami.Icon {
                    source: "audio-volume-high-symbolic"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }

                QQC2.Slider {
                    id: masterVolumeSlider
                    Layout.fillWidth: true
                    from: 0.0
                    to: 1.0
                    value: deviceModel.masterVolume

                    onPressedChanged: if (!pressed) {
                        deviceModel.setMasterVolume(value);
                    }

                    Timer {
                        interval: 60
                        repeat: true
                        running: masterVolumeSlider.pressed
                        onTriggered: deviceModel.setMasterVolume(masterVolumeSlider.value)
                    }
                }
            }

            // Test-Ton-Knopf zur Delay-Kalibrierung (PLAN.md Phase 7.2,
            // Nutzerwunsch): spielt einen wiederholten Klick über die
            // Kombi-Ausgabe ab, damit sich der Versatz zwischen Geräten
            // beim Verstellen des Delay-Reglers direkt heraushören lässt
            // (siehe PipeWireController::playTestTone).
            RowLayout {
                Layout.fillWidth: true
                enabled: deviceModel.toolEnabled

                Item {
                    Layout.fillWidth: true
                }

                QQC2.Button {
                    icon.name: "media-playback-start-symbolic"
                    text: i18n("Testton abspielen")
                    onClicked: pwController.playTestTone()
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            // Loader statt direkter Instanziierung: der häufige Fall (siehe
            // HANDOFF.md) ist, dass Geräte verbunden sind und dieser
            // Platzhalter dauerhaft unsichtbar bleibt - ohne Loader würde er
            // trotzdem bei jedem Popup-Öffnen mitgebaut, unnötiger Aufwand
            // beim (Wieder-)Öffnen (siehe PLAN.md Phase 7.1 „Mikro-Lag").
            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                active: deviceList.count === 0
                visible: active

                sourceComponent: PlasmaExtras.PlaceholderMessage {
                    iconName: "audio-speakers-symbolic"
                    text: i18n("No audio output devices found")
                }
            }

            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: deviceList.count > 0

                ListView {
                    id: deviceList
                    model: deviceModel
                    spacing: Kirigami.Units.smallSpacing

                    delegate: DeviceItem {
                        width: deviceList.width
                        deviceName: model.description.length > 0 ? model.description : model.name
                        formFactor: model.formFactor
                        btAddress: model.btAddress
                        codec: model.codec
                        active: model.active
                        volume: model.volume
                        delayMs: model.delayMs

                        onActiveToggled: (checked) => deviceModel.setActive(index, checked)
                        onVolumeEdited: (value) => deviceModel.setVolume(index, value)
                        onDelayEdited: (value) => deviceModel.setDelayMs(index, value)
                    }
                }
            }
        }
    }
}
