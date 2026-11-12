import QtQuick
import Qt.labs.folderlistmodel

Item {
    id: usageIcon

    property string kind: "ram"
    property string statePrefix: "ram-"
    property color iconColor: "#d8bafb"
    property bool showUsageLabel: true
    property bool showUsageFill: true
    property url stateFolder: Qt.resolvedUrl("workspace-state/")
    property string ramState: "0-0.0"
    readonly property int usagePercent: Math.max(0, Math.min(100,
        Number(ramState.split("-")[0]) || 0))
    readonly property string ramUsed: ramState.split("-")[1] || "0.0"

    function refreshRamState() {
        const filename = stateFiles.count > 0
            ? stateFiles.get(0, "fileName") : ""
        ramState = filename ? filename.slice(statePrefix.length) : "0-0.0"
    }

    width: 40
    height: 18

    FolderListModel {
        id: stateFiles
        folder: usageIcon.stateFolder
        nameFilters: [usageIcon.statePrefix + "*"]
        showFiles: true
        showDirs: false
        onCountChanged: usageIcon.refreshRamState()
        onStatusChanged: usageIcon.refreshRamState()
    }

    Connections {
        target: stateFiles
        function onDataChanged() { usageIcon.refreshRamState() }
        function onModelReset() { usageIcon.refreshRamState() }
    }

    component Glyph: Item {
        property string kind: "ram"
        property color fillColor: "white"

        // RAM stick
        Rectangle {
            visible: parent.kind === "ram"
            x: 3; y: 4; width: 14; height: 9
            color: parent.fillColor
        }
        Repeater {
            model: 4
            Rectangle {
                visible: parent.kind === "ram"
                x: 4 + index * 4; y: 13; width: 2; height: 3
                color: parent.fillColor
            }
        }

        // CPU package with pins on all sides
        Rectangle {
            visible: parent.kind === "cpu"
            x: 5; y: 4; width: 10; height: 10
            color: parent.fillColor
        }
        Repeater {
            model: 3
            Rectangle {
                visible: parent.kind === "cpu"
                x: 6 + index * 4; y: 1; width: 2; height: 3
                color: parent.fillColor
            }
        }
        Repeater {
            model: 3
            Rectangle {
                visible: parent.kind === "cpu"
                x: 6 + index * 4; y: 14; width: 2; height: 3
                color: parent.fillColor
            }
        }
        Repeater {
            model: 2
            Rectangle {
                visible: parent.kind === "cpu"
                x: 2; y: 6 + index * 4; width: 3; height: 2
                color: parent.fillColor
            }
        }
        Repeater {
            model: 2
            Rectangle {
                visible: parent.kind === "cpu"
                x: 15; y: 6 + index * 4; width: 3; height: 2
                color: parent.fillColor
            }
        }

        // Graphics card with bracket and connector
        Rectangle {
            visible: parent.kind === "gpu"
            x: 4; y: 4; width: 13; height: 10
            color: parent.fillColor
        }
        Rectangle {
            visible: parent.kind === "gpu"
            x: 2; y: 6; width: 2; height: 7
            color: parent.fillColor
        }
        Rectangle {
            visible: parent.kind === "gpu"
            x: 6; y: 14; width: 9; height: 2
            color: parent.fillColor
        }
    }

    Glyph {
        x: (parent.width - width) / 2
        y: 0
        width: 20
        height: 18
        kind: usageIcon.kind
        fillColor: usageIcon.iconColor
        opacity: usageIcon.showUsageFill ? 0.28 : 1
    }

    Item {
        visible: usageIcon.showUsageFill
        id: filledShape
        x: (parent.width - width) / 2
        y: parent.height * (100 - usageIcon.usagePercent) / 100
        width: 20
        height: parent.height * usageIcon.usagePercent / 100
        clip: true

        Glyph {
            x: 0
            y: -filledShape.y
            width: filledShape.width
            height: usageIcon.height
            kind: usageIcon.kind
            fillColor: usageIcon.iconColor
        }
    }

    Text {
        visible: usageIcon.kind === "ram" && usageIcon.showUsageLabel
        y: -6
        anchors.horizontalCenter: parent.horizontalCenter
        width: usageIcon.width
        height: 8
        text: usageIcon.ramUsed
        color: usageIcon.iconColor
        font.family: "Google Sans Flex"
        font.pixelSize: 8
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Component.onCompleted: refreshRamState()
}
