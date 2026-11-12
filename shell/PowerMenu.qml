import QtQuick
import QtQuick.Window

Window {
    id: powerMenu

    property color backgroundColor: "#151218"
    property color accentColor: "#d8bafb"
    property int buttonWidth: 48
    property int buttonHeight: 52
    property int buttonSpacing: 10
    property int horizontalPadding: 6
    property int verticalPadding: 6
    readonly property int panelWidth: buttonWidth + 2 * horizontalPadding
    readonly property int panelHeight: actions.length * buttonHeight
        + Math.max(0, actions.length - 1) * buttonSpacing
        + 2 * verticalPadding
    property int cornerRadius: 8
    property int connectorSize: 8
    readonly property var actions: [
        { icon: 0xF0343, action: "logout" },
        { icon: 0xF0425, action: "poweroff" },
        { icon: 0xF0709, action: "restart" }
    ]

    visible: powerMenuState.open
    color: "transparent"
    title: "VXWM Power Menu"
    flags: Qt.Tool
         | Qt.FramelessWindowHint
         | Qt.BypassWindowManagerHint
         | Qt.WindowStaysOnTopHint

    width: panelWidth
    height: panelHeight + 2 * connectorSize
    x: Screen.virtualX + Screen.width - width
    y: Screen.virtualY + (Screen.height - panelHeight) / 2 - connectorSize

    RightEdgePanel {
        panelWidth: powerMenu.panelWidth
        panelHeight: powerMenu.panelHeight
        cornerRadius: powerMenu.cornerRadius
        connectorSize: powerMenu.connectorSize
        backgroundColor: powerMenu.backgroundColor
    }

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        y: powerMenu.connectorSize + powerMenu.verticalPadding
        spacing: powerMenu.buttonSpacing

        Repeater {
            model: powerMenu.actions

            delegate: PowerActionButton {
                required property var modelData
                width: powerMenu.buttonWidth
                height: powerMenu.buttonHeight
                iconCodePoint: modelData.icon
                iconPixelSize: 25
                accentColor: powerMenu.accentColor
                accessibleName: modelData.action
                onTriggered: {
                    powerMenuState.close()
                    powerMenuState.triggerAction(modelData.action)
                }
            }
        }
    }

    Item {
        id: menuKeys
        anchors.fill: parent
        focus: true

        Keys.onEscapePressed: powerMenuState.close()
    }

    onVisibleChanged: {
        if (visible) {
            requestActivate()
            menuKeys.forceActiveFocus()
        }
    }
}
