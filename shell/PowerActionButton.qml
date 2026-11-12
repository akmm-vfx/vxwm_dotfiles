import QtQuick

Item {
    id: actionButton

    property int iconCodePoint: 0
    property int iconPixelSize: 20
    property color accentColor: "#d8bafb"
    property string accessibleName: ""
    signal triggered()

    width: 48
    height: 52

    Rectangle {
        anchors.fill: parent
        radius: 12
        color: actionButton.accentColor
        opacity: mouseArea.containsMouse ? 0.2 : 0.11
    }

    Text {
        anchors.centerIn: parent
        text: String.fromCodePoint(actionButton.iconCodePoint)
        color: actionButton.accentColor
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: actionButton.iconPixelSize
    }

    Accessible.name: actionButton.accessibleName
    Accessible.role: Accessible.Button

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: actionButton.triggered()
    }
}
