import QtQuick
import QtQuick.Window
import QtQuick.Shapes
import Qt.labs.folderlistmodel

Window {
    id: notificationContainer

    property color backgroundColor: "#211e24"
    property color accentColor: "#d8bafb"
    property int connectorSize: 7
    property int containerWidth: 320
    property url stateFolder: Qt.resolvedUrl("workspace-state/")
    property var notificationItems: []
    readonly property int containerHeight: cards.implicitHeight + 20

    visible: notificationItems.length > 0
    color: "transparent"
    title: "VXWM Notification Container"
    flags: Qt.Tool
         | Qt.FramelessWindowHint
         | Qt.BypassWindowManagerHint
         | Qt.WindowStaysOnTopHint

    width: containerWidth + connectorSize
    height: containerHeight + connectorSize
    x: Screen.virtualX + Screen.width - width - 4
    y: Screen.virtualY + 4

    FolderListModel {
        id: focusedWorkspace
        folder: notificationContainer.stateFolder
        nameFilters: ["focus-*"]
        showFiles: true
        showDirs: false
    }

    Shape {
        id: containerShape
        x: notificationContainer.connectorSize
        width: notificationContainer.containerWidth
        height: notificationContainer.containerHeight
        antialiasing: true

        ShapePath {
            fillColor: notificationContainer.backgroundColor
            strokeColor: "transparent"
            startX: 0
            startY: 0

            PathLine { x: containerShape.width; y: 0 }
            PathLine { x: containerShape.width; y: containerShape.height }
            PathLine { x: 12; y: containerShape.height }
            PathArc {
                x: 0
                y: containerShape.height - 12
                radiusX: 12
                radiusY: 12
                direction: PathArc.Clockwise
                useLargeArc: false
            }
            PathLine { x: 0; y: 0 }
        }
    }

    Column {
        id: cards
        x: containerShape.x + 10
        y: 10
        width: notificationContainer.containerWidth - 20
        spacing: 6

        Repeater {
            model: notificationContainer.notificationItems

            delegate: Rectangle {
                id: card
                required property var modelData
                readonly property var item: modelData
                property bool expanded: false

                Component.onCompleted: startDurationCountdown()
                onItemChanged: startDurationCountdown()

                function startDurationCountdown() {
                    durationTimer.stop()
                    if (item.preview || item.expiresAt <= 0) {
                        durationIcon.progress = 1
                    } else {
                        const remaining = Math.max(0, item.expiresAt - Date.now())
                        durationIcon.progress = 1 - remaining / 5000
                        if (remaining > 0)
                            durationTimer.start()
                    }
                    durationCanvas.requestPaint()
                }

                width: cards.width
                implicitHeight: content.implicitHeight + 18
                radius: 8
                color: Qt.lighter(notificationContainer.backgroundColor, 1.16)

                Timer {
                    interval: Math.max(1, card.item.expiresAt - Date.now())
                    running: !card.item.preview
                    onTriggered: notificationContainer.removeNotification(card.item.id, true)
                }

                Row {
                    id: content
                    x: 9
                    y: 9
                    width: parent.width - 18
                    spacing: 8

                    Item {
                        width: 20
                        height: 20

                        Image {
                            id: appIcon
                            anchors.fill: parent
                            source: card.item.appIcon
                            fillMode: Image.PreserveAspectFit
                            visible: status === Image.Ready
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            color: Qt.rgba(notificationContainer.accentColor.r,
                                notificationContainer.accentColor.g,
                                notificationContainer.accentColor.b, 0.18)
                            visible: !appIcon.visible

                            Text {
                                anchors.centerIn: parent
                                text: "i"
                                color: notificationContainer.accentColor
                                font.family: "Google Sans Flex"
                                font.pointSize: 9
                                font.weight: Font.Medium
                            }
                        }
                    }

                    Column {
                        width: parent.width - 28
                        spacing: 2

                        Row {
                            width: parent.width
                            spacing: 4

                            Text {
                                id: titleText
                                width: parent.width
                                    - (durationIcon.visible
                                        ? durationIcon.width + parent.spacing : 0)
                                    - expandButton.width - parent.spacing
                                text: (card.item.summary || card.item.appName || "Notification")
                                    + " · now"
                                color: "#e8e3ec"
                                font.family: "Google Sans Flex"
                                font.pointSize: 10
                                font.weight: Font.Medium
                                wrapMode: Text.Wrap
                                elide: card.expanded ? Text.ElideNone : Text.ElideRight
                                maximumLineCount: card.expanded ? 0 : 1
                            }

                            Item {
                                id: expandButton
                                width: 20
                                height: 20
                                visible: titleText.truncated || bodyText.truncated || card.expanded

                                Shape {
                                    anchors.centerIn: parent
                                    width: 14
                                    height: 14
                                    rotation: card.expanded ? 180 : 0
                                    transformOrigin: Item.Center
                                    antialiasing: true

                                    ShapePath {
                                        fillColor: "transparent"
                                        strokeColor: notificationContainer.accentColor
                                        strokeWidth: 1.8
                                        capStyle: ShapePath.RoundCap
                                        joinStyle: ShapePath.RoundJoin
                                        startX: 2
                                        startY: 5

                                        PathLine { x: 7; y: 10 }
                                        PathLine { x: 12; y: 5 }
                                    }
                                }

                                TapHandler {
                                    onTapped: card.expanded = !card.expanded
                                }
                            }

                            Item {
                                id: durationIcon
                                width: 20
                                height: 20
                                property real progress: 1

                                Canvas {
                                    id: durationCanvas
                                    anchors.centerIn: parent
                                    width: 14
                                    height: 14
                                    onPaint: {
                                        const context = getContext("2d")
                                        context.clearRect(0, 0, width, height)
                                        context.lineWidth = 1.8
                                        context.lineCap = "round"

                                        context.beginPath()
                                        context.strokeStyle = Qt.rgba(
                                            notificationContainer.accentColor.r,
                                            notificationContainer.accentColor.g,
                                            notificationContainer.accentColor.b, 0.25)
                                        context.arc(7, 7, 5, 0, Math.PI * 2, false)
                                        context.stroke()

                                        if (durationIcon.progress > 0) {
                                            context.beginPath()
                                            context.strokeStyle = notificationContainer.accentColor
                                            context.arc(7, 7, 5, -Math.PI / 2,
                                                -Math.PI / 2 + durationIcon.progress
                                                    * Math.PI * 2, false)
                                            context.stroke()
                                        }
                                    }
                                }

                                Timer {
                                    id: durationTimer
                                    interval: 50
                                    repeat: true
                                    onTriggered: {
                                        const remaining = Math.max(
                                            0, card.item.expiresAt - Date.now())
                                        durationIcon.progress = 1 - remaining / 5000
                                        durationCanvas.requestPaint()
                                        if (remaining === 0)
                                            stop()
                                    }
                                }
                            }

                        }

                        Text {
                            id: bodyText
                            width: parent.width
                            text: card.item.body
                            color: "#c7c2cc"
                            font.family: "Google Sans Flex"
                            font.pointSize: 10
                            wrapMode: Text.Wrap
                            maximumLineCount: card.expanded ? 0 : 3
                            elide: Text.ElideRight
                            visible: text.length > 0
                        }

                        Row {
                            spacing: 5
                            visible: card.item.actions.length > 0

                            Repeater {
                                model: Math.floor(card.item.actions.length / 2)

                                delegate: Rectangle {
                                    required property int index
                                    readonly property string actionKey: card.item.actions[index * 2]
                                    readonly property string actionText: card.item.actions[index * 2 + 1]
                                    width: actionLabel.implicitWidth + 12
                                    height: actionLabel.implicitHeight + 6
                                    radius: 5
                                    color: Qt.rgba(notificationContainer.accentColor.r,
                                        notificationContainer.accentColor.g,
                                        notificationContainer.accentColor.b, 0.12)

                                    Text {
                                        id: actionLabel
                                        anchors.centerIn: parent
                                        text: parent.actionText
                                        color: "#e8e3ec"
                                        font.family: "Google Sans Flex"
                                        font.pointSize: 10
                                    }

                                    TapHandler {
                                        onTapped: {
                                            notificationServer.invokeAction(card.item.id,
                                                parent.actionKey)
                                            notificationContainer.removeNotification(
                                                card.item.id, false)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    CornerShape {
        x: 0
        y: 0
        corner: "top-right"
        fillColor: notificationContainer.backgroundColor
        radius: notificationContainer.connectorSize
    }

    CornerShape {
        x: containerShape.x + containerShape.width - notificationContainer.connectorSize
        y: containerShape.y + containerShape.height
        corner: "top-right"
        fillColor: notificationContainer.backgroundColor
        radius: notificationContainer.connectorSize
    }

    Connections {
        target: notificationServer

        function onNotificationReceived(id, appName, appIcon, summary, body, actions) {
            const items = notificationContainer.notificationItems.slice()
            const existing = items.findIndex(function(item) { return item.id === id })
            const entry = {
                id: id,
                appName: appName,
                appIcon: appIcon,
                summary: summary,
                body: body,
                actions: actions,
                expiresAt: Date.now() + 5000
            }
            if (existing >= 0)
                items[existing] = entry
            else
                items.push(entry)

            const removed = []
            while (items.length > 3)
                removed.push(items.shift())
            notificationContainer.notificationItems = items
            for (let i = 0; i < removed.length; ++i)
                notificationServer.CloseNotification(removed[i].id)
        }

        function onNotificationClosed(id) {
            notificationContainer.removeNotification(id, false)
        }
    }

    function removeNotification(id, closeOnServer) {
        notificationItems = notificationItems.filter(function(item) {
            return item.id !== id
        })
        if (closeOnServer)
            notificationServer.CloseNotification(id)
    }
}
