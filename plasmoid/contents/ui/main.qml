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
// über ConfigStore pro Bluetooth-MAC persistiert. Der Delay-Regler
// schreibt aktuell nur ins Datenmodell: ein direkter Property-Ansatz
// (SPA_PROP_latencyOffsetNsec) hatte im Hörtest keine Wirkung, siehe
// SETUP.md - eine echte Latenzkompensation ist noch offen (PLAN.md
// Abschnitt 4).
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
        implicitWidth: Kirigami.Units.gridUnit * 20
        implicitHeight: Kirigami.Units.gridUnit * 16

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing

            PlasmaExtras.PlaceholderMessage {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: deviceList.count === 0
                iconName: "audio-speakers-symbolic"
                text: i18n("No Bluetooth audio devices connected")
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
