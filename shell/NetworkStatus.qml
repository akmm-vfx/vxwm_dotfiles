import QtQuick
import Qt.labs.folderlistmodel

Item {
    id: networkStatus

    property color iconColor: "#d8bafb"
    property url stateFolder: Qt.resolvedUrl("workspace-state/")

    width: 40
    height: 18

    FolderListModel {
        id: networkState
        folder: networkStatus.stateFolder
        nameFilters: ["network-*"]
        showFiles: true
        showDirs: false
    }

    Text {
        x: Math.round((parent.width - contentWidth) / 2) - 2.5
        width: contentWidth
        height: parent.height
        text: {
            if (networkState.count === 0)
                return String.fromCodePoint(0xF05AA)
            const state = networkState.get(0, "fileName").slice(8)
            if (state === "ethernet") return String.fromCodePoint(0xF0200)
            if (state === "wifi") return String.fromCodePoint(0xF05A9)
            return String.fromCodePoint(0xF05AA)
        }
        color: networkStatus.iconColor
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 18
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
