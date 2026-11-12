import QtQuick
import QtQuick.Window

Window {
    id: shell
    visible: true
    width: 1
    height: 1
    opacity: 0
    flags: Qt.Tool
         | Qt.FramelessWindowHint
         | Qt.WindowDoesNotAcceptFocus
         | Qt.WindowTransparentForInput

    property color barBackground: "#151218"
    property color barAccent: "#d8bafb"

    Loader {
        source: Qt.resolvedUrl("MatugenColors.qml")
        onLoaded: {
            shell.barBackground = item.background
            shell.barAccent = item.accent
        }
    }

    EdgeBar {
        edge: "left"
        backgroundColor: shell.barBackground
        accentColor: shell.barAccent
    }
    EdgeBar {
        edge: "top"
        backgroundColor: shell.barBackground
    }
    EdgeBar {
        edge: "right"
        backgroundColor: shell.barBackground
    }
    EdgeBar {
        edge: "bottom"
        backgroundColor: shell.barBackground
    }
    CornerConnector {
        corner: "top-left"
        backgroundColor: shell.barBackground
    }
    CornerConnector {
        corner: "top-right"
        backgroundColor: shell.barBackground
    }
    CornerConnector {
        corner: "bottom-left"
        backgroundColor: shell.barBackground
    }
    CornerConnector {
        corner: "bottom-right"
        backgroundColor: shell.barBackground
    }
    DesktopClock {
        textColor: shell.barAccent
    }
    LauncherPanel {
        backgroundColor: shell.barBackground
        textColor: shell.barAccent
    }
    NotificationPopup {
        backgroundColor: shell.barBackground
        accentColor: shell.barAccent
    }
    MediaOSD {
        backgroundColor: shell.barBackground
        accentColor: shell.barAccent
    }
    PowerMenu {
        backgroundColor: shell.barBackground
        accentColor: shell.barAccent
    }
}
