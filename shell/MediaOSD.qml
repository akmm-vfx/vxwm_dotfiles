import QtQuick
import QtQuick.Window

Window {
    id: mediaOSD

    property color backgroundColor: "#151218"
    property color accentColor: "#d8bafb"
    property int panelWidth: 48
    property int panelHeight: 306
    property int cornerRadius: 8
    property int connectorSize: 8
    property int previousBrightness: brightnessControl.brightness
    function showForActivity() {
        visible = true
        hideTimer.restart()
    }

    visible: false
    color: "transparent"
    title: "VXWM Media OSD"
    flags: Qt.Tool
         | Qt.FramelessWindowHint
         | Qt.BypassWindowManagerHint
         | Qt.WindowStaysOnTopHint
         | Qt.WindowDoesNotAcceptFocus

    width: panelWidth
    height: panelHeight + 2 * connectorSize
    x: Screen.virtualX + Screen.width - width
    y: Screen.virtualY + (Screen.height - panelHeight) / 2 - connectorSize

    RightEdgePanel {
        panelWidth: mediaOSD.panelWidth
        panelHeight: mediaOSD.panelHeight
        cornerRadius: mediaOSD.cornerRadius
        connectorSize: mediaOSD.connectorSize
        backgroundColor: mediaOSD.backgroundColor
    }

    SystemSlider {
        id: volumeSlider
        x: (mediaOSD.panelWidth - width) / 2
        y: 12
        sourceValue: audioVolume.volume
        symbol: audioVolume.muted || audioVolume.volume === 0
            ? String.fromCodePoint(0xF075F)
            : audioVolume.volume < 34
              ? String.fromCodePoint(0xF057F)
              : audioVolume.volume < 67
                ? String.fromCodePoint(0xF0580)
                : String.fromCodePoint(0xF057E)
        symbolOffset: -2
        muted: audioVolume.muted
        accentColor: mediaOSD.accentColor
        onValueRequested: audioVolume.setVolume(value)
        onInteractionStarted: hideTimer.stop()
        onInteractionEnded: hideTimer.restart()
    }

    Rectangle {
        x: (mediaOSD.panelWidth - width) / 2
        y: 157
        width: 3
        height: width
        radius: width / 2
        color: mediaOSD.accentColor
        opacity: 0.6
    }

    SystemSlider {
        id: brightnessSlider
        x: (mediaOSD.panelWidth - width) / 2
        y: 166
        sourceValue: brightnessControl.brightness
        symbol: "\uf185"
        symbolOffset: -3
        symbolPivotYOffset: 2.75
        accentColor: mediaOSD.accentColor
        onValueRequested: brightnessControl.setBrightness(value)
        onInteractionStarted: hideTimer.stop()
        onInteractionEnded: hideTimer.restart()
    }

    Connections {
        target: audioVolume
        function onVolumeChanged() {
            if (audioVolume.ready)
                mediaOSD.showForActivity()
        }
        function onMutedChanged() {
            if (audioVolume.ready)
                mediaOSD.showForActivity()
        }
    }

    Connections {
        target: brightnessControl
        function onBrightnessChanged() {
            if (brightnessControl.ready) {
                mediaOSD.showForActivity()
                const delta = brightnessControl.brightness - mediaOSD.previousBrightness
                mediaOSD.previousBrightness = brightnessControl.brightness
                brightnessSlider.rotateSymbolBy(delta)
            }
        }
    }

    Timer {
        id: hideTimer
        interval: 2000
        repeat: false
        onTriggered: {
            if (volumeSlider.dragging || brightnessSlider.dragging)
                restart()
            else
                mediaOSD.visible = false
        }
    }

}
