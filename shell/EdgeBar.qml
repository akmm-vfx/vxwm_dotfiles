import QtQuick
import QtQuick.Window

Window {
    id: bar

    property string edge: "left"
    property color backgroundColor: "#151218"
    property color accentColor: "#e0b8f6"
    property int statusIconGap: 6
    property int resourceSeparatorGap: 11
    property int timeSeparatorTopGap: 6

    visible: true
    color: "transparent"
    title: "VXWM Edge Bar " + edge
    flags: Qt.Tool
         | Qt.FramelessWindowHint
         | Qt.WindowStaysOnBottomHint
         | Qt.WindowDoesNotAcceptFocus
         | (edge === "left" ? 0 : Qt.WindowTransparentForInput)

    property int thickness: edge === "left" ? 40 : 4

    x: edge === "left" ? Screen.virtualX
       : edge === "right" ? Screen.virtualX + Screen.width - thickness
       : Screen.virtualX + 40
    y: edge === "top" ? Screen.virtualY
       : edge === "bottom" ? Screen.virtualY + Screen.height - thickness
       : edge === "right" ? Screen.virtualY + thickness
       : Screen.virtualY
    width: edge === "left" || edge === "right" ? thickness
         : Screen.width - 40
    height: edge === "top" || edge === "bottom" ? thickness
          : edge === "right" ? Screen.height - 2 * thickness
          : Screen.height

    Rectangle {
        anchors.fill: parent
        color: bar.backgroundColor
    }

    Text {
        visible: bar.edge === "left"
        x: -3
        y: 8
        width: bar.width
        height: 26
        text: "\uF303"
        color: bar.accentColor
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 22
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    WorkspaceDots {
        visible: bar.edge === "left"
        y: 45
        anchors.horizontalCenter: parent.horizontalCenter
        accentColor: bar.accentColor
    }

    BarDateTime {
        id: barDateTime
        visible: bar.edge === "left"
        y: parent.height - height - 8
        textColor: bar.accentColor
    }

    Rectangle {
        id: timeSeparator
        visible: bar.edge === "left"
        width: 3
        height: width
        radius: width / 2
        anchors.horizontalCenter: parent.horizontalCenter
        y: barDateTime.y - height - 8
        color: bar.accentColor
        opacity: 0.6
    }

    KeyboardLayout {
        id: keyboardLayout
        visible: bar.edge === "left"
        y: timeSeparator.y - height - bar.timeSeparatorTopGap
        textColor: bar.accentColor
    }

    NetworkStatus {
        id: networkIndicator
        visible: bar.edge === "left"
        y: keyboardLayout.y - height - bar.statusIconGap
        anchors.horizontalCenter: parent.horizontalCenter
        iconColor: bar.accentColor
    }

    Item {
        id: resourceMonitor
        visible: bar.edge === "left"
        y: networkIndicator.y - height - bar.resourceSeparatorGap
        anchors.horizontalCenter: parent.horizontalCenter
        width: 40
        height: 74

        Rectangle {
            x: 6
            y: 0
            width: 28
            height: 71
            radius: 8
            color: Qt.rgba(bar.accentColor.r,
                           bar.accentColor.g,
                           bar.accentColor.b, 0.10)
        }

        UsageIcon {
            y: 10
            anchors.horizontalCenter: parent.horizontalCenter
            kind: "ram"
            statePrefix: "ram-"
            iconColor: bar.accentColor
        }

        UsageIcon {
            y: 28
            anchors.horizontalCenter: parent.horizontalCenter
            kind: "cpu"
            statePrefix: "cpu-"
            iconColor: bar.accentColor
        }

        UsageIcon {
            y: 50
            anchors.horizontalCenter: parent.horizontalCenter
            kind: "gpu"
            statePrefix: "gpu-"
            iconColor: bar.accentColor
        }
    }

    TrayItems {
        id: trayItems
        visible: bar.edge === "left" && iconCount > 0
        y: resourceMonitor.y - height - bar.statusIconGap
        anchors.horizontalCenter: parent.horizontalCenter
    }

    Rectangle {
        id: resourceSeparator
        visible: bar.edge === "left"
        width: 3
        height: width
        radius: width / 2
        anchors.horizontalCenter: parent.horizontalCenter
        y: resourceMonitor.y + resourceMonitor.height
           + (bar.resourceSeparatorGap - height) / 2
        color: bar.accentColor
        opacity: 0.6
    }

    StatusIcon {
        id: muteIndicator
        visible: bar.edge === "left" && active
        stateFile: "audio-muted"
        codePoint: 0xF075F
        horizontalOffset: 0
        y: trayItems.visible
           ? trayItems.y - height - bar.statusIconGap
           : resourceMonitor.y - height - bar.statusIconGap
        anchors.horizontalCenter: parent.horizontalCenter
        iconColor: bar.accentColor
    }

    StatusIcon {
        id: capsIndicator
        visible: bar.edge === "left" && active
        stateFile: "caps-lock"
        codePoint: 0xF0A9B
        horizontalOffset: -0.5
        y: muteIndicator.visible
           ? muteIndicator.y - height - bar.statusIconGap
           : trayItems.visible
             ? trayItems.y - height - bar.statusIconGap
             : resourceMonitor.y - height - bar.statusIconGap
        anchors.horizontalCenter: parent.horizontalCenter
        iconColor: bar.accentColor
    }
}
