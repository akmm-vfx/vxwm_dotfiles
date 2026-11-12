import QtQuick
import Qt.labs.folderlistmodel

Item {
    id: statusIcon

    property string stateFile
    property int codePoint
    property real horizontalOffset: 0
    property color iconColor: "#d8bafb"
    property url stateFolder: Qt.resolvedUrl("workspace-state/")
    readonly property bool active: stateModel.count > 0

    width: 40
    height: 18

    FolderListModel {
        id: stateModel
        folder: statusIcon.stateFolder
        nameFilters: [statusIcon.stateFile]
        showFiles: true
        showDirs: false
    }

    visible: active

    Text {
        x: Math.round((parent.width - contentWidth) / 2) + statusIcon.horizontalOffset
        width: contentWidth
        height: parent.height
        text: String.fromCodePoint(statusIcon.codePoint)
        color: statusIcon.iconColor
        font.family: "JetBrainsMono Nerd Font"
        font.pixelSize: 18
        verticalAlignment: Text.AlignVCenter
    }
}
