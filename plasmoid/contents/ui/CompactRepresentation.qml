pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami

// Tray-Icon-Repräsentation. Zeigt per Icon-Wahl und Badge an, ob 0/1/mehrere
// Geräte aktiv in der Kombi-Ausgabe sind (siehe PLAN.md Abschnitt 3.4).
//
// Klick öffnet das Popup über plasmoidItem.expanded (als required property
// vom PlasmoidItem in main.qml hereingereicht) statt über den globalen
// Plasmoid-Kontext: Plasmoid.expanded existiert zwar als Property, hat
// aber offenbar keine Wirkung mehr auf das tatsächliche Popup (kein
// installiertes System-Plasmoid verwendet dieses Muster noch, siehe
// SETUP.md „Klick-öffnet-kein-Popup"-Fund).
MouseArea {
    id: compactRoot

    required property PlasmoidItem plasmoidItem
    property int activeCount: 0

    hoverEnabled: true

    QQC2.ToolTip.visible: containsMouse
    QQC2.ToolTip.text: compactRoot.activeCount === 0
        ? i18n("Keine Bluetooth-Audiogeräte aktiv")
        : i18np("%1 Gerät aktiv", "%1 Geräte aktiv", compactRoot.activeCount)

    Kirigami.Icon {
        id: icon
        anchors.fill: parent
        source: compactRoot.activeCount > 0 ? "audio-speakers-symbolic" : "audio-volume-muted-symbolic"
    }

    Rectangle {
        visible: compactRoot.activeCount > 1
        anchors.right: icon.right
        anchors.bottom: icon.bottom
        width: countLabel.implicitWidth + Kirigami.Units.smallSpacing
        height: countLabel.implicitHeight
        radius: height / 2
        color: Kirigami.Theme.highlightColor

        Text {
            id: countLabel
            anchors.centerIn: parent
            text: compactRoot.activeCount
            color: Kirigami.Theme.highlightedTextColor
            font.pointSize: Kirigami.Theme.smallFont.pointSize
        }
    }

    onClicked: compactRoot.plasmoidItem.expanded = !compactRoot.plasmoidItem.expanded
}
