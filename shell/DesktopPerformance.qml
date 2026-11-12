import QtQuick
import QtQuick.Window

Window {
    id: performanceWidget

    property color textColor: "#d8bafb"

    visible: true
    color: "transparent"
    title: "VXWM Desktop Performance"
    flags: Qt.BypassWindowManagerHint
         | Qt.FramelessWindowHint
         | Qt.WindowDoesNotAcceptFocus
         | Qt.WindowTransparentForInput
         | Qt.WindowStaysOnBottomHint

    width: 252
    height: 20 + cpuSection.height + gpuSection.height + content.spacing
    x: Screen.virtualX + Screen.width - width - 16
    y: Screen.virtualY + 16 + 57 + processRamMonitor.topProcesses.length * 21 + 8

    Column {
        id: content
        anchors.fill: parent
        anchors.margins: 10
        spacing: 18

        UsageSection {
            id: cpuSection
            width: content.width
            title: "CPU"
            totalUsage: processRamMonitor.cpuUsage + "%"
            processes: processRamMonitor.cpuProcesses
            emptyText: "Collecting process usage…"
        }

        UsageSection {
            id: gpuSection
            width: content.width
            title: "GPU"
            totalUsage: processRamMonitor.gpuUsage + "%"
            processes: processRamMonitor.gpuProcesses
            emptyText: "No active GPU apps"
        }
    }

    component UsageSection: Column {
        id: section
        property string title
        property string totalUsage
        property var processes
        property string emptyText
        property int valueColumnWidth: 56
        property int rows: Math.max(1, processes.length)
        height: 17 + 1 + rows * 17 + 4 * (rows + 1)
        spacing: 4

        Row {
            width: section.width
            height: 17

            Item {
                id: usageGlyphHost
                width: 20
                height: parent.height

                UsageIcon {
                    anchors.centerIn: parent
                    width: 20
                    height: 18
                    kind: section.title.toLowerCase()
                    statePrefix: section.title.toLowerCase() + "-"
                    iconColor: performanceWidget.textColor
                    showUsageLabel: false
                    showUsageFill: false
                }
            }

            Text {
                text: section.title
                color: performanceWidget.textColor
                font.family: "Google Sans Flex"
                font.pixelSize: 12
                font.weight: Font.Medium
                width: parent.width - usageGlyphHost.width - totalLabel.implicitWidth
                height: parent.height
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                id: totalLabel
                text: section.totalUsage
                color: performanceWidget.textColor
                font.family: "Google Sans Flex"
                font.pixelSize: 12
                horizontalAlignment: Text.AlignRight
                height: parent.height
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            width: section.width
            height: 1
            color: performanceWidget.textColor
            opacity: 0.65
        }

        Repeater {
            model: section.processes.length
                ? section.processes
                : [{ name: section.emptyText, usage: "" }]

            delegate: Item {
                required property var modelData
                width: section.width
                height: 17

                Text {
                    text: modelData.name
                    anchors.left: parent.left
                    anchors.right: usageText.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    color: performanceWidget.textColor
                    font.family: "Google Sans Flex"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                Text {
                    id: usageText
                    text: modelData.usage
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: section.valueColumnWidth
                    color: performanceWidget.textColor
                    font.family: "Google Sans Flex"
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignRight
                }
            }
        }
    }
}
