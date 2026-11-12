import QtQuick

Item {
    id: systemSlider

    property int sourceValue: 0
    property int displayValue: sourceValue
    property string symbol: ""
    property real symbolOffset: 0
    property real symbolPivotYOffset: 0
    property color accentColor: "#d8bafb"
    property bool muted: false
    property bool dragging: false
    signal valueRequested(int value)
    signal interactionStarted()
    signal interactionEnded()

    width: 40
    height: 138

    function rotateSymbolBy(valueDelta) {
        symbolAnimation.stop()
        symbolAnimation.from = symbolRotation.angle
        symbolAnimation.to = symbolRotation.angle + valueDelta * 0.9
        symbolAnimation.start()
    }

    onSourceValueChanged: {
        if (!dragging)
            displayValue = sourceValue
    }

    Text {
        id: symbolText
        x: (parent.width - width) / 2 + systemSlider.symbolOffset
        y: 0
        width: 18
        height: 18
        horizontalAlignment: Text.AlignHCenter
        text: systemSlider.symbol
        color: systemSlider.accentColor
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 18
        transform: Rotation {
            id: symbolRotation
            origin.x: systemSlider.width / 2 - symbolText.x
            origin.y: symbolText.height / 2 + systemSlider.symbolPivotYOffset
            angle: 0
        }
    }

    RotationAnimation {
        id: symbolAnimation
        target: symbolRotation
        property: "angle"
        from: 0
        to: 0
        duration: 900
        easing.type: Easing.OutCubic
    }

    Rectangle {
        id: track
        x: (parent.width - width) / 2
        y: 30
        width: 8
        height: 102
        radius: width / 2
        color: "#35ffffff"
        opacity: systemSlider.muted ? 0.35 : 1

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: track.height * systemSlider.displayValue / 100
            radius: parent.radius
            color: systemSlider.accentColor
        }
    }

    Rectangle {
        width: 14
        height: 14
        x: (parent.width - width) / 2
        y: track.y + (track.height - height)
            * (100 - systemSlider.displayValue) / 100
        radius: width / 2
        color: systemSlider.accentColor
        opacity: systemSlider.muted ? 0.35 : 1
    }

    MouseArea {
        anchors.fill: parent

        function updateValue(yPosition) {
            systemSlider.displayValue = Math.round(Math.max(0,
                Math.min(1, 1 - (yPosition - track.y) / track.height)) * 100)
            commitTimer.restart()
        }

        onPressed: mouse => {
            systemSlider.dragging = true
            systemSlider.interactionStarted()
            updateValue(mouse.y)
        }
        onPositionChanged: mouse => {
            if (pressed)
                updateValue(mouse.y)
        }
        onReleased: {
            systemSlider.dragging = false
            commitTimer.stop()
            systemSlider.valueRequested(systemSlider.displayValue)
            systemSlider.interactionEnded()
        }
    }

    Timer {
        id: commitTimer
        interval: 120
        repeat: false
        onTriggered: {
            if (systemSlider.dragging)
                systemSlider.valueRequested(systemSlider.displayValue)
        }
    }
}
