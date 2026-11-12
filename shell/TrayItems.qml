import QtQuick
import Qt.labs.folderlistmodel

Item {
    id: trayItems

    property url stateFolder: Qt.resolvedUrl("workspace-state/")
    property int iconCount: trayFiles.count
    property int iconSize: 18
    property int separatorSize: 3
    property int separatorMargin: 4

    width: 40
    height: iconCount > 0
        ? trayIcons.implicitHeight + separatorMargin * 2 + separatorSize
        : 0

    FolderListModel {
        id: trayFiles
        folder: trayItems.stateFolder
        nameFilters: ["tray-*.png"]
        showFiles: true
        showDirs: false
    }

    Column {
        id: trayIcons
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 4

        Repeater {
            model: trayFiles

            Image {
                id: trayIcon
                required property int index
                width: trayItems.iconSize
                height: trayItems.iconSize
                source: trayFiles.get(index, "fileUrl")
                fillMode: Image.PreserveAspectFit
                cache: false

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function(mouse) {
                        const filename = trayFiles.get(index, "fileName")
                        const match = /^tray-(\d+)\.png$/.exec(filename)
                        if (!match)
                            return
                        const position = trayIcon.mapToGlobal(Qt.point(mouse.x, mouse.y))
                        trayControl.invokeTrayItem(Number(match[1]),
                            mouse.button === Qt.RightButton,
                            Math.round(position.x), Math.round(position.y))
                    }
                }
            }
        }
    }

    Rectangle {
        visible: trayItems.iconCount > 0
        width: trayItems.separatorSize
        height: width
        radius: width / 2
        anchors.horizontalCenter: parent.horizontalCenter
        y: trayIcons.implicitHeight + trayItems.separatorMargin
        color: "#888888"
    }
}
