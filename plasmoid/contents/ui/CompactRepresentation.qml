pragma ComponentBehavior: Bound

import QtQuick
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

    // Kein Hover-Tooltip: er tauchte sofort beim Hovern auf und lag dabei
    // über dem Icon, was das eigentliche Öffnen des Popups per Klick
    // erschwerte (Nutzer-Feedback) - der Badge-Zähler unten liefert die
    // gleiche Information rein visuell, ohne den Klickpfad zu stören.

    Kirigami.Icon {
        id: icon
        anchors.fill: parent
        // Eigenes Icon (zwei Lautsprecher nebeneinander statt generischem
        // Einzel-Lautsprecher) im "aktiv"-Zustand, damit sich das Tray-Icon
        // von anderen Audio-Widgets unterscheidet (Nutzerwunsch, PLAN.md
        // Phase 7.2). Für "aus" bewusst beim Standard-Mute-Icon geblieben -
        // das Konzept "stummgeschaltet" ist bereits universell klar und
        // braucht keine eigene Bildsprache.
        source: compactRoot.activeCount > 0 ? "multiaudiooutput-symbolic" : "audio-volume-muted-symbolic"
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
