import QtQuick

Item {
    id: dateTime

    property color textColor: "#d8bafb"
    property date currentTime: new Date()

    width: 40
    height: 38

    Column {
        anchors.centerIn: parent
        spacing: 0

        Repeater {
            model: ["HH", "mm"]

            Text {
                required property string modelData
                width: dateTime.width
                height: 18
                text: Qt.formatDateTime(dateTime.currentTime, modelData)
                color: dateTime.textColor
                font.family: "Google Sans Flex"
                font.pointSize: 10
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    Timer {
        interval: 30000
        repeat: true
        running: true
        onTriggered: dateTime.currentTime = new Date()
    }
}
