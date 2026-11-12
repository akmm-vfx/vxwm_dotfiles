import QtQuick
import QtQuick.Controls
import Qt.labs.folderlistmodel

Item {
    id: dots

    property color accentColor: "#d8bafb"
    property url stateFolder: Qt.resolvedUrl("workspace-state/")

    width: 40
    height: workspaceList.implicitHeight

    FolderListModel {
        id: focusedWorkspace
        folder: dots.stateFolder
        nameFilters: ["focus-*"]
        showFiles: true
        showDirs: false
    }

    Column {
        id: workspaceList

        y: 0
        width: parent.width
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 5

        Repeater {
            model: 5

            delegate: Item {
                id: workspace
                required property int index

                property bool focused: index === focusedWorkspace.count - 1
                property int shownCount: Math.min(apps.count, 4)

                width: 40
                height: apps.count === 0 ? 14
                      : shownCount * 20 + (apps.count > 4 ? 12 : 0) + 4

                function iconName(iconIndex) {
                    const filename = apps.get(iconIndex, "fileName")
                    return filename.split("-").slice(4).join("-")
                }

                FolderListModel {
                    id: apps
                    folder: dots.stateFolder
                    nameFilters: ["app-" + workspace.index + "-*"]
                    showFiles: true
                    showDirs: false
                }

                Rectangle {
                    visible: apps.count > 0
                    x: (parent.width - 26) / 2
                    width: 26
                    height: parent.height
                    radius: 8
                    color: Qt.rgba(dots.accentColor.r,
                                   dots.accentColor.g,
                                   dots.accentColor.b,
                                   workspace.focused ? 0.20 : 0.10)
                }

                Rectangle {
                    anchors.centerIn: parent
                    visible: apps.count === 0
                    width: workspace.focused ? 6 : 4
                    height: width
                    radius: width / 2
                    color: workspace.focused ? dots.accentColor : "#888888"
                }

                Column {
                    y: apps.count > 0 ? 2 : 0
                    width: parent.width
                    height: apps.count > 0 ? parent.height - 4 : parent.height

                    Repeater {
                        model: workspace.shownCount

                        delegate: ToolButton {
                            required property int index

                            width: workspace.width
                            height: 20
                            padding: 0
                            enabled: false
                            display: AbstractButton.IconOnly
                            icon.name: workspace.iconName(index) === "chatgpt"
                                       ? "application-x-executable"
                                       : workspace.iconName(index)
                            icon.source: workspace.iconName(index) === "chatgpt"
                                         ? "file:///usr/share/pixmaps/chatgpt.png"
                                         : ""
                            icon.width: workspace.focused ? 18 : 16
                            icon.height: icon.width
                            icon.color: "transparent"
                            background: null
                        }
                    }

                    Text {
                        visible: apps.count > 4
                        width: workspace.width
                        height: 12
                        text: "+" + (apps.count - 4)
                        color: workspace.focused ? dots.accentColor : "#aaaaaa"
                        font.family: "Google Sans Flex"
                        font.pixelSize: 9
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

            }
        }
    }
}
