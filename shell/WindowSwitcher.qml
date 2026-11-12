import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Qt.labs.folderlistmodel

Window {
    id: switcher

    property color backgroundColor: "#151218"
    property color accentColor: "#d8bafb"
    readonly property url stateFolder: Qt.resolvedUrl("workspace-state/")
    readonly property int rowHeight: 56
    readonly property int panelWidth: 440
    readonly property int searchHeight: 40
    readonly property int maxVisibleRows: 5
    property bool selectionInitialized: false
    property string searchText: ""
    property var titleCache: ({})

    visible: openMarker.count > 0
    color: "transparent"
    title: "VXWM Window Switcher"
    flags: Qt.Tool
         | Qt.FramelessWindowHint
         | Qt.BypassWindowManagerHint
         | Qt.WindowStaysOnTopHint

    x: virtualScreenGeometry.x + (virtualScreenGeometry.width - width) / 2
    y: virtualScreenGeometry.y + (virtualScreenGeometry.height - height) / 2
    width: panelWidth
    height: Math.min(Math.max(searchHeight + rowHeight + 30,
                              searchHeight + Math.min(filteredWindows.count,
                                  maxVisibleRows) * rowHeight + 30),
                     virtualScreenGeometry.height * 0.8)

    FolderListModel {
        id: openMarker
        folder: switcher.stateFolder
        nameFilters: ["window-switcher-open"]
        showFiles: true
        showDirs: false
    }

    FolderListModel {
        id: windows
        folder: switcher.stateFolder
        nameFilters: ["window-??????????-*"]
        showFiles: true
        showDirs: false
        sortField: FolderListModel.Name
        onCountChanged: Qt.callLater(switcher.rebuildFilteredWindows)
    }

    ListModel {
        id: filteredWindows
    }

    Connections {
        target: windows
        function onDataChanged() { switcher.rebuildFilteredWindows() }
        function onModelReset() { switcher.rebuildFilteredWindows() }
        function onStatusChanged() { switcher.rebuildFilteredWindows() }
    }

    onSearchTextChanged: rebuildFilteredWindows()

    function rebuildFilteredWindows() {
        const query = searchText.trim().toLocaleLowerCase()
        filteredWindows.clear()
        for (let i = 0; i < windows.count; ++i) {
            const fileName = windows.get(i, "fileName")
            const parts = fileName.split("-")
            const iconName = parts.length >= 7
                ? parts.slice(6).join("-") : "application-x-executable"
            const name = displayName(iconName)
            const title = parts.length >= 3
                ? titleForWindow(parts[2]) : ""
            if (!query || name.toLocaleLowerCase().includes(query)
                || iconName.toLocaleLowerCase().includes(query)
                || title.toLocaleLowerCase().includes(query))
                filteredWindows.append({ fileName: fileName })
        }
        rows.currentIndex = filteredWindows.count > 0 ? 0 : -1
        if (filteredWindows.count > 0)
            rows.positionViewAtIndex(rows.currentIndex, ListView.Contain)
        Qt.callLater(initializeSelection)
    }

    function fieldsAt(index) {
        if (index < 0 || index >= filteredWindows.count)
            return []
        return filteredWindows.get(index).fileName.split("-")
    }

    function selectNext() {
        if (filteredWindows.count > 0) {
            rows.currentIndex = (rows.currentIndex + 1 + filteredWindows.count)
                % filteredWindows.count
            rows.positionViewAtIndex(rows.currentIndex, ListView.Contain)
        }
    }

    function initializeSelection() {
        if (!visible || selectionInitialized || filteredWindows.count === 0)
            return
        let activeIndex = -1
        for (let i = 0; i < filteredWindows.count; ++i) {
            if (fieldsAt(i)[5] === "1") {
                activeIndex = i
                break
            }
        }
        rows.currentIndex = activeIndex >= 0 ? activeIndex : 0
        if (activeIndex >= 0)
            selectNext()
        selectionInitialized = true
    }

    function activateSelection() {
        const fields = fieldsAt(rows.currentIndex)
        if (fields.length >= 7)
            windowSwitcher.activateWindow(fields[2])
        windowSwitcher.closeSwitcher()
    }

    function closeWithoutActivation() {
        windowSwitcher.closeSwitcher()
    }

    function displayName(iconName) {
        switch (iconName) {
        case "org.kde.dolphin": return "Dolphin"
        case "kitty": return "Kitty"
        case "zen-browser": return "Zen Browser"
        case "firefox": return "Firefox"
        case "chromium-browser": return "Chromium"
        case "blender": return "Blender"
        case "chatgpt": return "ChatGPT"
        case "vscodium": return "VSCodium"
        case "code": return "Visual Studio Code"
        case "spotify-client": return "Spotify"
        case "org.xfce.Thunar": return "Thunar"
        case "org.gnome.Nautilus": return "Files"
        default: return iconName === "application-x-executable"
            ? "Application" : iconName
        }
    }

    function titleForWindow(windowId) {
        if (titleCache[windowId] === undefined)
            titleCache[windowId] = windowSwitcher.windowTitle(windowId)
        return titleCache[windowId]
    }

    Rectangle {
        anchors.fill: parent
        radius: 12
        color: switcher.backgroundColor

        Item {
            id: content
            anchors.fill: parent
            anchors.margins: 8

            TextField {
                id: searchField
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: switcher.searchHeight
                leftPadding: 40
                rightPadding: 64
                placeholderText: "Search windows"
                placeholderTextColor: Qt.rgba(switcher.accentColor.r,
                                               switcher.accentColor.g,
                                               switcher.accentColor.b, 0.65)
                color: switcher.accentColor
                font.family: "Google Sans Flex"
                font.pixelSize: 13
                selectByMouse: true
                onTextChanged: switcher.searchText = text

                background: Rectangle {
                    radius: 8
                    color: Qt.rgba(switcher.accentColor.r,
                                   switcher.accentColor.g,
                                   switcher.accentColor.b, 0.1)
                }

                Text {
                    x: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: "\uf002"
                    color: switcher.accentColor
                    font.family: "JetBrainsMono Nerd Font"
                    font.pixelSize: 15
                }

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    width: 48
                    horizontalAlignment: Text.AlignRight
                    text: Math.min(filteredWindows.count,
                                   Math.max(1, Math.floor(rows.height
                                       / switcher.rowHeight)))
                        + " / " + windows.count
                    color: switcher.accentColor
                    opacity: 0.72
                    font.family: "Google Sans Flex"
                    font.pixelSize: 10
                }

                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Escape) {
                        switcher.closeWithoutActivation()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Down) {
                        switcher.selectNext()
                        event.accepted = true
                    } else if (event.key === Qt.Key_Up) {
                        if (filteredWindows.count > 0) {
                            rows.currentIndex = (rows.currentIndex - 1
                                + filteredWindows.count) % filteredWindows.count
                            rows.positionViewAtIndex(rows.currentIndex, ListView.Contain)
                        }
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return
                               || event.key === Qt.Key_Enter) {
                        switcher.activateSelection()
                        event.accepted = true
                    }
                }
            }

            ListView {
                id: rows
                anchors.top: searchField.bottom
                anchors.topMargin: 6
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                clip: true
                model: filteredWindows
                currentIndex: -1
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height

                ScrollBar.vertical: ScrollBar {
                    policy: rows.contentHeight > rows.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    width: 6
                    z: 2

                    background: Rectangle {
                        radius: 3
                        color: Qt.rgba(switcher.accentColor.r,
                                       switcher.accentColor.g,
                                       switcher.accentColor.b, 0.12)
                    }
                    contentItem: Rectangle {
                        implicitWidth: 6
                        radius: 2
                        color: switcher.accentColor
                        opacity: 0.85
                    }
                }

                delegate: Item {
                    id: row
                    required property string fileName
                    required property int index
                    property var fields: fileName.split("-")
                    property string iconName: fields.length >= 7
                        ? fields.slice(6).join("-") : "application-x-executable"
                    property string windowTitle: fields.length >= 3
                        ? switcher.titleForWindow(fields[2]) : ""
                    property bool pinned: fields.length >= 7 && fields[3] === "1"
                    property bool minimized: fields.length >= 7 && fields[4] === "1"

                    width: rows.width
                    height: switcher.rowHeight

                    Rectangle {
                        anchors.fill: parent
                        radius: 8
                        color: rows.currentIndex === row.index
                            ? Qt.rgba(switcher.accentColor.r,
                                      switcher.accentColor.g,
                                      switcher.accentColor.b, 0.12)
                            : "transparent"
                    }

                    Image {
                        x: 10
                        width: 28
                        height: 28
                        anchors.verticalCenter: parent.verticalCenter
                        source: windowSwitcher.iconPath(row.iconName)
                        sourceSize.width: 64
                        sourceSize.height: 64
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        asynchronous: true
                    }

                    Column {
                        x: 50
                        width: parent.width - x - 78
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1

                        Text {
                            width: parent.width
                            text: switcher.displayName(row.iconName)
                            color: switcher.accentColor
                            font.family: "Google Sans Flex"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }

                        Text {
                            width: parent.width
                            text: row.windowTitle
                            color: switcher.accentColor
                            opacity: 0.72
                            font.family: "Google Sans Flex"
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            visible: row.windowTitle.length > 0
                        }
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 30
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.pinned ? "\uf08d" : ""
                        color: switcher.accentColor
                        font.family: "JetBrainsMono Nerd Font"
                        font.pixelSize: 16
                        visible: row.pinned
                        Accessible.name: "Pinned"
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.minimized ? "\uf078" : ""
                        color: switcher.accentColor
                        font.family: "JetBrainsMono Nerd Font"
                        font.pixelSize: 14
                        visible: row.minimized
                        Accessible.name: "Minimized"
                    }

                    MouseArea {
                        anchors.fill: parent
                        onEntered: rows.currentIndex = row.index
                        onClicked: switcher.activateSelection()
                    }
                }
            }

            Text {
                anchors.centerIn: rows
                text: "No windows found"
                color: switcher.accentColor
                opacity: 0.65
                font.family: "Google Sans Flex"
                font.pixelSize: 12
                visible: filteredWindows.count === 0
            }
        }
    }

    onVisibleChanged: {
        if (visible) {
            searchField.clear()
            titleCache = ({})
            rows.currentIndex = -1
            selectionInitialized = false
            Qt.callLater(function() {
                rebuildFilteredWindows()
                initializeSelection()
                requestActivate()
                searchField.forceActiveFocus()
            })
        } else {
            rows.currentIndex = -1
        }
    }
}
