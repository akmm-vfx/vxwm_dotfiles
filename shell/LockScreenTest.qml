import QtQuick
import QtQuick.Window
import QtQuick.Shapes
import Qt.labs.folderlistmodel

Window {
    id: testLock
    property color backgroundColor: "#211e24"
    property color accentColor: "#d8bafb"
    visible: lockScreenState.locked
    width: Screen.width
    height: Screen.height
    x: Screen.virtualX
    y: Screen.virtualY
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.BypassWindowManagerHint
         | Qt.WindowStaysOnTopHint

    Image {
        anchors.fill: parent
        source: lockScreenState.locked
            ? Qt.resolvedUrl("workspace-state/lock-screen-background.png") : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: false
        cache: false
    }

    Rectangle {
        anchors.fill: parent
        color: "#80000000"
    }

    Shape {
        id: notch
        x: (parent.width - width) / 2
        width: 204
        height: 46
        antialiasing: true

        ShapePath {
            fillColor: Qt.rgba(testLock.backgroundColor.r,
                testLock.backgroundColor.g, testLock.backgroundColor.b, 0.94)
            strokeColor: "transparent"
            startX: 0
            startY: 0
            PathLine { x: notch.width; y: 0 }
            PathLine { x: notch.width; y: notch.height - 12 }
            PathCubic {
                control1X: notch.width
                control1Y: notch.height - 5
                control2X: notch.width - 5
                control2Y: notch.height
                x: notch.width - 12
                y: notch.height
            }
            PathLine { x: 12; y: notch.height }
            PathCubic {
                control1X: 5
                control1Y: notch.height
                control2X: 0
                control2Y: notch.height - 5
                x: 0
                y: notch.height - 12
            }
            PathLine { x: 0; y: 0 }
        }
    }

    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 4
        spacing: 14

        FolderListModel {
            id: capsLockState
            folder: Qt.resolvedUrl("workspace-state/")
            nameFilters: ["caps-lock"]
            showFiles: true
            showDirs: false
        }

        Text {
            visible: capsLockState.count > 0
            text: String.fromCodePoint(0xF030F)
            color: testLock.accentColor
            font.family: "JetBrainsMono Nerd Font"
            font.pixelSize: 17
        }

        Text {
            text: systemKeyboardLayout.name.toUpperCase()
            color: testLock.accentColor
            font.family: "Google Sans Flex"
            font.pointSize: 10
        }
    }

    Rectangle {
        id: passwordCard
        width: Math.min(360, testLock.width - 40)
        height: 164
        anchors.centerIn: parent
        radius: 16
        color: Qt.rgba(testLock.backgroundColor.r,
            testLock.backgroundColor.g, testLock.backgroundColor.b, 0.94)

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            Text {
                text: "Unlock"
                color: testLock.accentColor
                font.family: "Google Sans Flex"
                font.pointSize: 13
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Rectangle {
                width: parent.width
                height: 42
                radius: 10
                color: Qt.rgba(testLock.accentColor.r,
                    testLock.accentColor.g, testLock.accentColor.b, 0.1)

                TextInput {
                    id: passwordInput
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    verticalAlignment: TextInput.AlignVCenter
                    horizontalAlignment: TextInput.AlignHCenter
                    echoMode: TextInput.Password
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                    passwordMaskCharacter: "•"
                    color: testLock.accentColor
                    font.family: "Google Sans Flex"
                    font.pointSize: 12
                    focus: true
                }
            }
        }
    }
}
