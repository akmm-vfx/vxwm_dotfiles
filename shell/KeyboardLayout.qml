import QtQuick

Item {
    id: layoutIndicator

    property color textColor: "#d8bafb"

    width: 40
    height: 18

    Text {
        anchors.fill: parent
        text: systemKeyboardLayout.name
        color: layoutIndicator.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
