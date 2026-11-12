import QtQuick
import QtQuick.Window
import QtQuick.Shapes
import QtQuick.Effects
import Qt.labs.folderlistmodel

Window {
    id: lockScreen

    property color backgroundColor: "#211e24"
    property color accentColor: "#d8bafb"
    property url stateFolder: Qt.resolvedUrl("workspace-state/")
    property url backgroundImage: Qt.resolvedUrl("workspace-state/lock-screen-background.png")
    property int capsLockKey: Qt.Key_CapsLock
    property int layoutToggleKey: Qt.Key_Space
    property int layoutToggleModifier: Qt.MetaModifier
    property bool passwordVisible: false
    property int notchHorizontalPadding: 9
    property int notchVerticalPadding: 5

    function stopTypingAnimations() {
        blurSweepAnimation.stop()
        blurWaveAnimation.stop()
        backgroundShakeAnimation.stop()
        blurSweep.waveOffset = 0
        backgroundShake.x = 0
        backgroundShake.y = 0
        blurSweep.visible = false
    }

    function togglePasswordVisibility() {
        passwordVisible = !passwordVisible
        stopTypingAnimations()
        passwordInput.forceActiveFocus()
    }

    visible: lockScreenState.locked
    color: "#000000"
    title: "VXWM Lock Screen"
    flags: Qt.Tool
         | Qt.FramelessWindowHint
         | Qt.BypassWindowManagerHint
         | Qt.WindowStaysOnTopHint

    width: Screen.width
    height: Screen.height
    x: Screen.virtualX
    y: Screen.virtualY

    onVisibleChanged: {
        if (visible) {
            requestActivate()
            passwordInput.forceActiveFocus()
            grabRetry.start()
        } else {
            grabRetry.stop()
            stopTypingAnimations()
            lockScreenState.releaseInput(lockScreen)
        }
    }

    Image {
        anchors.fill: parent
        anchors.margins: -5
        source: lockScreenState.locked ? lockScreen.backgroundImage : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: false
        cache: false
        transform: Translate { id: backgroundShake }
    }

    Rectangle {
        anchors.fill: parent
        color: "#59000000"
    }

    Shape {
        id: notch
        x: (parent.width - width) / 2
        width: indicators.implicitWidth + lockScreen.notchHorizontalPadding * 2
        height: indicators.implicitHeight + lockScreen.notchVerticalPadding * 2
        antialiasing: true

        ShapePath {
            fillColor: Qt.rgba(lockScreen.backgroundColor.r,
                lockScreen.backgroundColor.g, lockScreen.backgroundColor.b, 0.94)
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

        Row {
            id: indicators
            anchors.centerIn: parent
            spacing: 10

            FolderListModel {
                id: capsLockState
                folder: lockScreen.stateFolder
                nameFilters: ["caps-lock"]
                showFiles: true
                showDirs: false
            }

            Text {
                id: lockIcon
                text: String.fromCodePoint(0xF033E)
                color: lockScreen.accentColor
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 19
                anchors.verticalCenter: parent.verticalCenter
                transform: Translate { id: lockIconOffset }
            }

            Rectangle {
                width: 3
                height: 3
                radius: 1.5
                color: lockScreen.accentColor
                opacity: 0.55
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: String.fromCodePoint(0xF0A9B)
                color: lockScreen.accentColor
                opacity: capsLockState.count > 0 ? 1 : 0.35
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 20
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: systemKeyboardLayout.name
                color: lockScreen.accentColor
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    TextInput {
        id: passwordInput
        width: 1
        height: 1
        opacity: 0
        anchors.centerIn: parent
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
        enabled: lockScreenState.inputGrabbed && !lockScreenState.verifying
        focus: true

        onTextChanged: {
            if (text.length === 0) {
                lockScreen.stopTypingAnimations()
            } else {
                backgroundShake.x = 0
                backgroundShake.y = 0
                backgroundShakeAnimation.restart()
                if (lockScreen.passwordVisible)
                    Qt.callLater(startBlurSweep)
            }
        }

        function startBlurSweep() {
            if (!lockScreen.passwordVisible || text.length === 0)
                return
            blurSweep.x = visiblePassword.x - blurSweep.width
            blurSweep.waveOffset = 0
            blurSweep.visible = true
            blurSweepAnimation.stop()
            blurSweepAnimation.to = visiblePassword.x + visiblePassword.width
            blurSweepAnimation.start()
            blurWaveAnimation.restart()
        }

        Keys.onPressed: event => {
            if (event.key === lockScreen.capsLockKey) {
                event.accepted = true
            } else if (event.key === lockScreen.layoutToggleKey
                    && (event.modifiers
                        & (lockScreen.layoutToggleModifier | Qt.SuperModifier))) {
                lockScreenState.cycleKeyboardLayout()
                event.accepted = true
            } else if ((event.key === Qt.Key_H || event.key === Qt.Key_J
                        || event.key === Qt.Key_K)
                    && (event.modifiers & Qt.ControlModifier)) {
                lockScreen.togglePasswordVisibility()
                event.accepted = true
            } else if (event.modifiers
                    & (Qt.MetaModifier | Qt.ControlModifier | Qt.AltModifier)) {
                event.accepted = true
            }
        }
        Keys.onReturnPressed: submitPassword()
        Keys.onEnterPressed: submitPassword()

        function submitPassword() {
            if (text.length === 0 || lockScreenState.verifying
                    || !lockScreenState.inputGrabbed)
                return
            const candidate = text
            clear()
            errorLabel.authenticationFailed = false
            lockScreenState.verifyPassword(candidate)
        }
    }

    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: lockScreen.visible
        onActivated: {
            passwordInput.clear()
            lockScreen.stopTypingAnimations()
            lockScreenState.closeLockScreen()
        }
    }

    Text {
        id: visiblePassword
        anchors.centerIn: parent
        text: passwordInput.text
        color: lockScreen.accentColor
        font.family: "Google Sans Flex"
        font.pointSize: 14
        width: implicitWidth
        z: 2
        visible: lockScreen.passwordVisible && passwordInput.text.length > 0
    }

    Item {
        id: blurSweep
        property real waveOffset: 0
        width: 52
        height: visiblePassword.height + 16
        y: visiblePassword.y - 8 + waveOffset
        clip: true
        visible: false
        z: 3

        MultiEffect {
            id: blurredPassword
            x: visiblePassword.x - blurSweep.x
            y: 8
            width: visiblePassword.width
            height: visiblePassword.height
            source: visiblePassword
            blurEnabled: true
            blur: 1.1
            opacity: 0.95
        }
    }

    SequentialAnimation {
        id: backgroundShakeAnimation
        ParallelAnimation {
            NumberAnimation {
                target: backgroundShake
                property: "x"
                to: 1.5
                duration: 70
                easing.type: Easing.OutSine
            }
            NumberAnimation {
                target: backgroundShake
                property: "y"
                to: 0.8
                duration: 70
                easing.type: Easing.OutSine
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: backgroundShake
                property: "x"
                to: -1
                duration: 75
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                target: backgroundShake
                property: "y"
                to: -0.5
                duration: 75
                easing.type: Easing.InOutSine
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: backgroundShake
                property: "x"
                to: 0.5
                duration: 75
                easing.type: Easing.InOutSine
            }
            NumberAnimation {
                target: backgroundShake
                property: "y"
                to: 0.25
                duration: 75
                easing.type: Easing.InOutSine
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: backgroundShake
                property: "x"
                to: 0
                duration: 80
                easing.type: Easing.OutSine
            }
            NumberAnimation {
                target: backgroundShake
                property: "y"
                to: 0
                duration: 80
                easing.type: Easing.OutSine
            }
        }
    }

    NumberAnimation {
        id: blurSweepAnimation
        target: blurSweep
        property: "x"
        duration: 260
        easing.type: Easing.OutCubic
        onFinished: blurSweep.visible = false
    }

    SequentialAnimation {
        id: blurWaveAnimation
        NumberAnimation {
            target: blurSweep
            property: "waveOffset"
            to: -8
            duration: 40
            easing.type: Easing.OutSine
        }
        NumberAnimation {
            target: blurSweep
            property: "waveOffset"
            to: 8
            duration: 55
            easing.type: Easing.InOutSine
        }
        NumberAnimation {
            target: blurSweep
            property: "waveOffset"
            to: -5
            duration: 60
            easing.type: Easing.InOutSine
        }
        NumberAnimation {
            target: blurSweep
            property: "waveOffset"
            to: 0
            duration: 65
            easing.type: Easing.OutSine
        }
    }

    Connections {
        target: lockScreen
        function onPasswordVisibleChanged() {
            if (!lockScreen.passwordVisible)
                lockScreen.stopTypingAnimations()
        }
    }

    Text {
        id: passwordVisibilityIcon
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.rightMargin: 22
        text: String.fromCodePoint(lockScreen.passwordVisible ? 0xF0208 : 0xF0209)
        color: lockScreen.accentColor
        opacity: 0.8
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 19
        anchors.topMargin: (notch.height - height) / 2

        MouseArea {
            anchors.fill: parent
            anchors.margins: -8
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                lockScreen.togglePasswordVisibility()
            }
        }
    }

    SequentialAnimation {
        id: lockShake
        NumberAnimation { target: lockIconOffset; property: "x"; to: 4; duration: 45 }
        NumberAnimation { target: lockIconOffset; property: "x"; to: -4; duration: 70 }
        NumberAnimation { target: lockIconOffset; property: "x"; to: 3; duration: 60 }
        NumberAnimation { target: lockIconOffset; property: "x"; to: -2; duration: 50 }
        NumberAnimation { target: lockIconOffset; property: "x"; to: 0; duration: 45 }
    }

    Text {
        id: errorLabel
        property bool authenticationFailed: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        text: errorLabel.authenticationFailed
            ? "Password not accepted"
            : lockScreenState.verifying ? "Checking…"
            : lockScreenState.inputGrabbed ? "Type password and press Enter"
            : "Securing keyboard input…"
        color: errorLabel.authenticationFailed ? "#ffb4ab" : lockScreen.accentColor
        opacity: 0.7
        font.family: "Google Sans Flex"
        font.pointSize: 9
    }

    Timer {
        id: grabRetry
        interval: 250
        repeat: true
        onTriggered: {
            if (lockScreenState.grabInput(lockScreen)) {
                if (!passwordInput.activeFocus)
                    passwordInput.forceActiveFocus()
            }
        }
    }

    Connections {
        target: lockScreenState
        function onAuthenticationFailed() {
            errorLabel.authenticationFailed = true
            lockShake.restart()
            passwordInput.forceActiveFocus()
        }
    }
}
