import QtQuick
import QtQuick.Window

Window {
    id: ramWidget

    property color textColor: "#d8bafb"

    visible: true
    color: "transparent"
    title: "VXWM Desktop RAM Usage"
    flags: Qt.BypassWindowManagerHint
         | Qt.FramelessWindowHint
         | Qt.WindowDoesNotAcceptFocus
         | Qt.WindowTransparentForInput
         | Qt.WindowStaysOnBottomHint

    width: 252
    height: 57 + processRamMonitor.topProcesses.length * 21
    x: Screen.virtualX + Screen.width - width - 16
    y: Screen.virtualY + 16

    Column {
        id: content
        anchors.fill: parent
        anchors.margins: 10
        spacing: 4

        Item {
            width: content.width
            height: 32

            UsageIcon {
                width: 20
                height: 18
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                kind: "ram"
                statePrefix: "ram-"
                iconColor: ramWidget.textColor
                showUsageLabel: false
                showUsageFill: false
            }

            Text {
                text: "Ram"
                color: ramWidget.textColor
                font.family: "Google Sans Flex"
                font.pixelSize: 12
                font.weight: Font.Medium
                anchors.left: parent.left
                anchors.leftMargin: 24
                anchors.verticalCenter: parent.verticalCenter
            }

            Column {
                id: summaryColumn
                width: Math.max(summaryLabel.implicitWidth, summaryValue.implicitWidth)
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1

                Text {
                    id: summaryLabel
                    text: "Used / Total"
                    color: ramWidget.textColor
                    font.family: "Google Sans Flex"
                    font.pixelSize: 9
                    horizontalAlignment: Text.AlignRight
                    width: parent.width
                }

                Text {
                    id: summaryValue
                    text: processRamMonitor.memorySummary
                    color: ramWidget.textColor
                    font.family: "Google Sans Flex"
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignRight
                    width: parent.width
                }
            }
        }

        Rectangle {
            width: content.width
            height: 1
            color: ramWidget.textColor
            opacity: 0.65
        }

        Repeater {
            model: processRamMonitor.topProcesses

            delegate: Row {
                required property var modelData
                width: content.width
                height: 17
                spacing: 8

                Text {
                    text: modelData.name
                    color: ramWidget.textColor
                    font.family: "Google Sans Flex"
                    font.pixelSize: 12
                    elide: Text.ElideLeft
                    horizontalAlignment: Text.AlignLeft
                    width: content.width - memoryLabel.implicitWidth - 8
                }

                Text {
                    id: memoryLabel
                    text: modelData.memory
                    color: ramWidget.textColor
                    font.family: "Google Sans Flex"
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
    }
}
