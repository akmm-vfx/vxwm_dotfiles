import QtQuick
import QtQuick.Controls
import QtQuick.Window
import Qt.labs.folderlistmodel

Window {
    id: launcher

    property color backgroundColor: "#151218"
    property color textColor: "#d8bafb"
    property url stateFolder: Qt.resolvedUrl("workspace-state/")
    property bool dismissed: false
    property int contentWidth: 420
    property int cornerRadius: 7
    property int inputHeight: 48
    property int rowHeight: 56
    property int maxVisibleRows: 8
    property int maxClipboardHeight: 420
    readonly property int visibleRowLimit: maxVisibleRows
    readonly property int searchResultTotal: appCatalog.totalCount
    readonly property int shownResultCount: {
        if (!appCatalog.clipboardMode && appCatalog.filterText.length === 0)
            return 0
        if (appCatalog.count === 0 || results.height <= 0)
            return 0
        if (!appCatalog.clipboardMode)
            return Math.min(appCatalog.count, maxVisibleRows)
        let usedHeight = 0
        let shown = 0
        const limit = Math.min(appCatalog.count, maxVisibleRows)
        while (shown < limit && usedHeight < results.height) {
            usedHeight += appCatalog.entryHeight(shown)
            ++shown
        }
        return shown
    }
    readonly property int resultsHeight: {
        appCatalog.clipboardRevision
        if (!appCatalog.clipboardMode && appCatalog.filterText.length === 0)
            return 0
        if (appCatalog.count === 0)
            return rowHeight
        let total = 0
        const rowCount = Math.min(appCatalog.count, visibleRowLimit)
        for (let i = 0; i < rowCount; ++i)
            total += appCatalog.entryHeight(i)
        const maxHeight = appCatalog.clipboardMode
            ? Math.min(maxClipboardHeight, Screen.height - inputHeight - 8)
            : Screen.height - inputHeight - 8
        return Math.min(total, maxHeight)
    }
    readonly property color selectionColor: Qt.rgba(
        textColor.r, textColor.g, textColor.b, 0.1)

    visible: launcherState.count > 0 && !dismissed
    color: "transparent"
    title: "VXWM Application Launcher"
    flags: Qt.Tool
         | Qt.FramelessWindowHint
         | Qt.BypassWindowManagerHint
         | Qt.WindowStaysOnTopHint

    x: Screen.virtualX + (Screen.width - width) / 2
    y: Screen.virtualY + 4
    width: contentWidth + 2 * cornerRadius
    height: inputHeight + resultsHeight

    FolderListModel {
        id: launcherState
        folder: launcher.stateFolder
        nameFilters: ["launcher-open"]
        showFiles: true
        showDirs: false
        onCountChanged: {
            if (count === 0)
                launcher.dismissed = false
        }
    }

    FolderListModel {
        id: focusedWorkspace
        folder: launcher.stateFolder
        nameFilters: ["focus-*"]
        showFiles: true
        showDirs: false
        onCountChanged: {
            if (launcher.visible)
                Qt.callLater(function() { launcher.raise() })
        }
    }

    FolderListModel {
        id: clipboardLaunchRequest
        folder: launcher.stateFolder
        nameFilters: ["launcher-clipboard-request-*"]
        showFiles: true
        showDirs: false
        onCountChanged: {
            if (count === 0)
                return
            if (appCatalog.clipboardMode && launcher.visible) {
                launcher.dismissed = true
            } else {
                input.text = ""
                appCatalog.filterText = ""
                appCatalog.clipboardMode = true
                results.currentIndex = appCatalog.count > 0 ? 0 : -1
            }
            appCatalog.consumeClipboardLaunchRequests()
        }
    }

    Item {
        id: clipArea
        anchors.fill: parent
        clip: true

        Rectangle {
            id: panel
            x: launcher.cornerRadius
            width: launcher.contentWidth
            height: launcher.height
            y: -height
            radius: 12
            color: launcher.backgroundColor

            Rectangle {
                width: parent.width
                height: parent.radius
                color: parent.color
            }

            TextInput {
                id: input
                x: 52
                y: 0
                width: parent.width - 132
                height: launcher.inputHeight
                verticalAlignment: TextInput.AlignVCenter
                font.family: "Google Sans Flex"
                font.pointSize: 10
                color: launcher.textColor
                selectionColor: launcher.selectionColor
                focus: true

                Keys.onEscapePressed: launcher.dismissed = true
                Keys.onDownPressed: {
                    if ((appCatalog.clipboardMode || appCatalog.filterText.length > 0)
                            && appCatalog.count > 0) {
                        results.currentIndex = Math.min(
                            results.currentIndex + 1, appCatalog.count - 1)
                        results.positionViewAtIndex(results.currentIndex, ListView.Contain)
                        event.accepted = true
                    }
                }
                Keys.onUpPressed: {
                    if ((appCatalog.clipboardMode || appCatalog.filterText.length > 0)
                            && appCatalog.count > 0) {
                        results.currentIndex = Math.max(
                            0, results.currentIndex - 1)
                        results.positionViewAtIndex(results.currentIndex, ListView.Contain)
                        event.accepted = true
                    }
                }
                Keys.onReturnPressed: {
                    if ((appCatalog.clipboardMode || appCatalog.filterText.length > 0)
                            && results.currentIndex >= 0) {
                        if (appCatalog.clipboardMode)
                            appCatalog.copyClipboard(results.currentIndex)
                        else
                            appCatalog.launch(results.currentIndex)
                    }
                    event.accepted = true
                }
                Keys.onEnterPressed: {
                    if ((appCatalog.clipboardMode || appCatalog.filterText.length > 0)
                            && results.currentIndex >= 0) {
                        if (appCatalog.clipboardMode)
                            appCatalog.copyClipboard(results.currentIndex)
                        else
                            appCatalog.launch(results.currentIndex)
                    }
                    event.accepted = true
                }
                onTextChanged: {
                    if (!appCatalog.clipboardMode && text.startsWith(";")) {
                        appCatalog.clipboardMode = true
                        text = text.slice(1)
                    }
                    appCatalog.filterText = text
                    results.currentIndex = (appCatalog.clipboardMode
                        || appCatalog.filterText.length > 0)
                        && appCatalog.count > 0 ? 0 : -1
                }

                Text {
                    anchors.fill: parent
                    verticalAlignment: Text.AlignVCenter
                    text: appCatalog.clipboardMode ? "Search in clipboard" : "Search in apps"
                    color: launcher.textColor
                    opacity: 0.55
                    font: input.font
                    visible: input.text.length === 0
                }
            }

            Text {
                x: parent.width - 70
                y: 0
                width: 56
                height: launcher.inputHeight
                text: launcher.shownResultCount + " / " + launcher.searchResultTotal
                color: launcher.textColor
                opacity: 0.72
                font.family: "Google Sans Flex"
                font.pointSize: 9
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }

            Text {
                x: 16
                y: 0
                width: 26
                height: launcher.inputHeight
                text: appCatalog.clipboardMode ? "\uf0ea" : "\uf002"
                color: launcher.textColor
                font.family: "JetBrainsMono Nerd Font"
                font.pixelSize: 16
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            ListView {
                id: results
                y: launcher.inputHeight
                width: parent.width
                height: launcher.resultsHeight
                clip: true
                model: appCatalog
                currentIndex: -1
                boundsBehavior: Flickable.StopAtBounds
                interactive: contentHeight > height

                ScrollBar.vertical: ScrollBar {
                    policy: results.contentHeight > results.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                    width: 6
                    z: 2

                    background: Rectangle {
                        radius: 3
                        color: Qt.rgba(launcher.textColor.r,
                                       launcher.textColor.g,
                                       launcher.textColor.b, 0.12)
                    }
                    contentItem: Rectangle {
                        implicitWidth: 6
                        radius: 2
                        color: launcher.textColor
                        opacity: 0.8
                    }
                }

                delegate: Item {
                    id: row
                    required property int index
                    readonly property string appName: {
                        const count = appCatalog.count
                        return index >= 0 && index < count
                            ? appCatalog.entryName(index) : ""
                    }
                    readonly property string appVersion: {
                        const count = appCatalog.count
                        return index >= 0 && index < count
                            ? appCatalog.entryVersion(index) : ""
                    }
                    readonly property string iconPath: {
                        const count = appCatalog.count
                        return index < count ? appCatalog.entryIconPath(index) : ""
                    }
                    readonly property int rowHeight: {
                        appCatalog.clipboardMode
                        appCatalog.clipboardRevision
                        return appCatalog.clipboardMode
                            ? appCatalog.entryHeight(index) : launcher.rowHeight
                    }
                    readonly property bool imageClipboardItem:
                        appCatalog.clipboardMode && iconPath.length > 0
                    readonly property int iconSize: imageClipboardItem ? 108 : 36

                    width: results.width
                    height: row.rowHeight

                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        anchors.topMargin: 3
                        anchors.bottomMargin: 3
                        radius: 8
                        color: results.currentIndex === row.index
                            ? launcher.selectionColor : "transparent"
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 14
                        anchors.rightMargin: 14
                        spacing: 10

                        Image {
                            width: row.iconSize
                            height: row.iconSize
                            anchors.verticalCenter: parent.verticalCenter
                            source: row.iconPath
                            fillMode: Image.PreserveAspectFit
                            smooth: row.imageClipboardItem
                            asynchronous: true
                            visible: source.toString().length > 0
                        }

                        Text {
                            visible: row.iconPath.length === 0
                            width: row.iconSize
                            height: row.iconSize
                            anchors.verticalCenter: parent.verticalCenter
                            text: appCatalog.clipboardMode ? "\uf0ea" : "◈"
                            color: launcher.textColor
                            font.family: appCatalog.clipboardMode
                                ? "JetBrainsMono Nerd Font" : "Google Sans Flex"
                            font.pixelSize: appCatalog.clipboardMode ? 19 : 24
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                    }

                    Text {
                        visible: !row.imageClipboardItem
                        x: 60
                        width: parent.width - x - 14
                        y: appCatalog.clipboardMode ? 6 : 8
                        height: appCatalog.clipboardMode ? parent.height - 12 : 18
                        text: row.appName.length > 0 ? row.appName : "Unknown application"
                        color: launcher.textColor
                        font.family: "Google Sans Flex"
                        font.pointSize: 10
                        textFormat: Text.PlainText
                        wrapMode: appCatalog.clipboardMode ? Text.Wrap : Text.NoWrap
                        maximumLineCount: appCatalog.clipboardMode ? 4 : 1
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    Text {
                        visible: !appCatalog.clipboardMode && !row.imageClipboardItem
                        x: 60
                        y: 27
                        width: parent.width - x - 14
                        height: 14
                        text: row.appVersion.length > 0
                            ? row.appVersion : "Version unavailable"
                        color: launcher.textColor
                        font.family: "Google Sans Flex"
                        font.pointSize: 8
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: results.currentIndex = row.index
                        onClicked: {
                            if (appCatalog.clipboardMode)
                                appCatalog.copyClipboard(row.index)
                            else
                                appCatalog.launch(row.index)
                        }
                    }
                }
            }

            Text {
                x: 14
                y: launcher.inputHeight
                width: parent.width - 28
                height: launcher.resultsHeight
                text: appCatalog.clipboardMode ? "No clipboard items" : "No matching apps"
                color: launcher.textColor
                font.family: "Google Sans Flex"
                font.pointSize: 10
                verticalAlignment: Text.AlignVCenter
                visible: (appCatalog.clipboardMode || appCatalog.filterText.length > 0)
                    && appCatalog.count === 0
            }
        }

        CornerShape {
            x: 0
            y: panel.y
            corner: "top-right"
            fillColor: launcher.backgroundColor
            radius: launcher.cornerRadius
        }

        CornerShape {
            x: panel.x + panel.width
            y: panel.y
            corner: "top-left"
            fillColor: launcher.backgroundColor
            radius: launcher.cornerRadius
        }
    }

    NumberAnimation {
        id: dropAnimation
        target: panel
        property: "y"
        to: 0
        duration: 170
        easing.type: Easing.OutCubic
    }

    onVisibleChanged: {
        if (visible) {
            input.text = ""
            appCatalog.clipboardMode = false
            appCatalog.filterText = ""
            results.currentIndex = -1
            panel.y = -panel.height
            dropAnimation.start()
            requestActivate()
            input.forceActiveFocus()
        } else {
            dropAnimation.stop()
            panel.y = -panel.height
        }
    }

    onActiveChanged: {
        if (active)
            input.forceActiveFocus()
    }

    Connections {
        target: appCatalog
        function onApplicationLaunched() {
            launcher.dismissed = true
        }
    }
}
