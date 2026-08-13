import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

// Eine Zeile in der Geräteliste des Popups. Bindet gegen die Rollen aus
// DeviceListModel (src/devicelistmodel.h).
ColumnLayout {
    id: deviceItem

    property string deviceName: ""
    property string formFactor: ""
    property string btAddress: ""
    property string codec: ""
    property bool active: false
    property real volume: 1.0
    property int delayMs: 0

    // Icon je nach Geräteart. device.form-factor wird auf dem Testsystem
    // aktuell für kein einziges Gerät (auch nicht Bluetooth) gesetzt - mit
    // echtem pw-dump verifiziert, siehe SETUP.md - daher primär anhand
    // btAddress unterscheiden (Bluetooth vs. nicht) und formFactor nur als
    // Verfeinerung nutzen, falls eine Distribution/ein Gerät ihn doch
    // liefert.
    readonly property string deviceIcon: {
        switch (formFactor) {
        case "headset":
        case "headphone":
            return "audio-headset-symbolic";
        case "speaker":
        case "hifi":
        case "car":
            return "audio-speakers-symbolic";
        case "handset":
        case "phone":
            return "phone-symbolic";
        case "portable":
            return "multimedia-player-symbolic";
        }
        return btAddress.length > 0 ? "audio-headset-symbolic" : "audio-card-symbolic";
    }

    // Der Delay-Wert wird über CombineSinkManager per module-loopback
    // (target.delay.sec) tatsächlich im Audiopfad wirksam (siehe PLAN.md
    // Phase 7.1, SETUP.md „Delay-Ergebnis Teil 2"). Der zuvor verworfene
    // direkte Property-Ansatz (SPA_PROP_latencyOffsetNsec) hatte im
    // Hörtest keine Wirkung.
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
            id: volumeSlider
            from: 0.0
            to: 1.0
            value: deviceItem.volume
            Layout.preferredWidth: Kirigami.Units.gridUnit * 10

            // Ein reiner Debounce (Timer bei jeder Bewegung neu gestartet,
            // feuert erst nach einer Ruhephase) fühlte sich bei kleinen
            // Bewegungen wie "reagiert nicht" an - jede neue Mausbewegung
            // schiebt den Timer immer weiter auf, bevor er auslösen kann.
            // Jetzt stattdessen ein Throttle: Während des Ziehens (pressed)
            // wird alle 60ms der aktuelle Wert angewendet, das bleibt spürbar
            // "live", begrenzt aber trotzdem die wpctl-Aufrufe (statt einem
            // pro Drag-Tick). Beim Loslassen wird der finale Wert zusätzlich
            // sofort angewendet, ohne auf den nächsten Tick zu warten.
            onPressedChanged: if (!pressed) {
                deviceItem.volumeEdited(value);
            }

            Timer {
                interval: 60
                repeat: true
                running: volumeSlider.pressed
                onTriggered: deviceItem.volumeEdited(volumeSlider.value)
            }
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
            id: delaySpinBox
            from: 0
            to: 2000
            stepSize: 5
            value: deviceItem.delayMs

            // Debounce wie beim Lautstärke-Regler, hier sogar wichtiger:
            // jede Delay-Änderung löst ein vollständiges Neuladen von
            // combine-stream aus (siehe CombineSinkManager::setActiveTargets),
            // was kurz *alle* aktiven Geräte unterbricht, nicht nur dieses.
            // Ohne Debounce würde Halten der Auf/Ab-Buttons (Auto-Repeat)
            // eine Reload-Kaskade auslösen.
            onValueModified: delayDebounce.restart()

            Timer {
                id: delayDebounce
                interval: 150
                onTriggered: deviceItem.delayEdited(delaySpinBox.value)
            }
        }
        QQC2.Label {
            text: i18n("ms")
        }
    }
}
