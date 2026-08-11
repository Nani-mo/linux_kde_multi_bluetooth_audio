import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// Eine Zeile in der Geräteliste des Popups. Bindet gegen die Rollen aus
// DeviceListModel (src/devicelistmodel.h).
ColumnLayout {
    id: deviceItem

    property string deviceName: ""
    property string deviceIcon: "audio-headset-symbolic"
    property string codec: ""
    property bool active: false
    property real volume: 1.0
    property int delayMs: 0

    // Der Delay-Wert wird aktuell nur im Datenmodell gespeichert - ein
    // direkter Property-Ansatz (SPA_PROP_latencyOffsetNsec) hatte im
    // Hörtest keine Wirkung; eine tatsächliche Latenz-Kompensation im
    // Audiopfad ist noch offen (siehe PLAN.md Abschnitt 4, SETUP.md).
    signal activeToggled(bool checked)
    signal volumeEdited(real value)
    signal delayEdited(int value)

    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true

        Kirigami.Icon {
            source: deviceItem.deviceIcon
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
        }

        QQC2.CheckBox {
            checked: deviceItem.active
            onToggled: deviceItem.activeToggled(checked)
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            QQC2.Label {
                Layout.fillWidth: true
                text: deviceItem.deviceName
                elide: Text.ElideRight
            }
            QQC2.Label {
                Layout.fillWidth: true
                visible: deviceItem.codec.length > 0
                text: deviceItem.codec.toUpperCase()
                elide: Text.ElideRight
                opacity: 0.6
                font.pointSize: Kirigami.Theme.smallFont.pointSize
            }
        }

        QQC2.Slider {
            from: 0.0
            to: 1.0
            value: deviceItem.volume
            Layout.preferredWidth: Kirigami.Units.gridUnit * 6
            onMoved: deviceItem.volumeEdited(value)
        }

        QQC2.ToolButton {
            // Text statt icon.name: Icon-Themes stellen "arrow-up"/"arrow-down"
            // nicht überall bereit (in plasmoidviewer z.B. unsichtbar, aber
            // klickbar) - Unicode-Pfeile sind immer sichtbar.
            text: delayExpander.visible ? "▲" : "▼"
            checkable: true
            checked: delayExpander.visible
            onToggled: delayExpander.visible = checked
        }
    }

    RowLayout {
        id: delayExpander
        visible: false
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing

        QQC2.Label {
            text: i18n("Delay-Offset:")
        }
        QQC2.SpinBox {
            from: 0
            to: 500
            stepSize: 5
            value: deviceItem.delayMs
            onValueModified: deviceItem.delayEdited(value)
        }
        QQC2.Label {
            text: i18n("ms")
        }
    }
}
