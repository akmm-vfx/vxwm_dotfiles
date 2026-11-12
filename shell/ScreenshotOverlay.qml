import QtQuick

Item {
    id: overlay

    width: captureController.screenWidth
    height: captureController.screenHeight

    property color accent: "#d8bafb"
    property color background: "#211e24"
    property real startX: 0
    property real startY: 0
    property rect selection: Qt.rect(0, 0, 0, 0)
    property bool dragging: false
    property bool drawing: false
    property real strokeWidth: 4
    property var strokes: []
    property var hoveredWindow: null
    property var selectedWindow: null
    property var pressedWindow: null
    property var detectedWindows: captureController.detectedWindows
    property string statusText: "Hover and click a window, or drag a rectangle · Ctrl+C copy · Ctrl+S save · Esc close"

    function windowAt(x, y) {
        for (let i = 0; i < detectedWindows.length; ++i) {
            const candidate = detectedWindows[i]
            if (x >= candidate.x && x <= candidate.x + candidate.width
                    && y >= candidate.y && y <= candidate.y + candidate.height)
                return candidate
        }
        return null
    }

    function selectWindow(candidate) {
        hoveredWindow = candidate
        selectedWindow = candidate
        if (!candidate) {
            selection = Qt.rect(0, 0, 0, 0)
            return
        }
        selection = Qt.rect(candidate.x, candidate.y, candidate.width, candidate.height)
        strokes = []
    }

    function updateDrag(x, y) {
        const dx = x - startX
        const dy = y - startY
        selection = Qt.rect(Math.min(startX, x), Math.min(startY, y),
                            Math.abs(dx), Math.abs(dy))
    }

    function containsSelection(x, y) {
        const inside = x >= selection.x && x <= selection.x + selection.width
            && y >= selection.y && y <= selection.y + selection.height
        return inside
    }

    function addStrokePoint(x, y) {
        if (strokes.length === 0)
            return
        const updated = strokes.slice()
        const current = updated[updated.length - 1]
        current.points = current.points.concat([{ "x": x, "y": y }])
        updated[updated.length - 1] = current
        strokes = updated
        drawingCanvas.requestPaint()
    }

    onSelectionChanged: {
        drawingCanvas.requestPaint()
        screenShade.requestPaint()
    }

    Loader {
        source: Qt.resolvedUrl("MatugenColors.qml")
        onLoaded: {
            overlay.accent = item.accent
            overlay.background = item.background
        }
    }

    Image {
        id: capturedScreen
        anchors.fill: parent
        source: "image://capture/frame"
        fillMode: Image.Stretch
        cache: false
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
    }

    Item {
        x: overlay.selection.x
        y: overlay.selection.y
        width: overlay.selection.width
        height: overlay.selection.height
        clip: true
        visible: width > 1 && height > 1

        Image {
            x: -overlay.selection.x
            y: -overlay.selection.y
            width: overlay.width
            height: overlay.height
            source: "image://capture/frame"
            fillMode: Image.Stretch
            cache: false
            visible: false
        }

        Canvas {
            id: drawingCanvas
            anchors.fill: parent
            renderTarget: Canvas.FramebufferObject

            onPaint: {
                const context = getContext("2d")
                context.clearRect(0, 0, width, height)
                context.save()
                for (let i = 0; i < overlay.strokes.length; ++i) {
                    const stroke = overlay.strokes[i]
                    const points = stroke.points
                    if (points.length === 0)
                        continue
                    context.strokeStyle = stroke.color
                    context.fillStyle = stroke.color
                    context.lineWidth = stroke.width
                    context.lineCap = "round"
                    context.lineJoin = "round"
                    context.beginPath()
                    context.moveTo(points[0].x - overlay.selection.x,
                                   points[0].y - overlay.selection.y)
                    for (let j = 1; j < points.length; ++j)
                        context.lineTo(points[j].x - overlay.selection.x,
                                       points[j].y - overlay.selection.y)
                    if (points.length === 1) {
                        context.arc(points[0].x - overlay.selection.x,
                                    points[0].y - overlay.selection.y,
                                    stroke.width / 2, 0, Math.PI * 2)
                        context.fill()
                    } else {
                        context.stroke()
                    }
                }
                context.restore()
            }
        }
    }

    Canvas {
        id: screenShade
        anchors.fill: parent
        z: 2
        renderTarget: Canvas.FramebufferObject

        onPaint: {
            const context = getContext("2d")
            context.clearRect(0, 0, width, height)
            context.globalCompositeOperation = "source-over"
            context.fillStyle = "#99000000"
            context.fillRect(0, 0, width, height)
            if (overlay.selection.width <= 1 || overlay.selection.height <= 1)
                return
            context.globalCompositeOperation = "destination-out"
            context.beginPath()
            if (overlay.selectedWindow) {
                const x = overlay.selection.x
                const y = overlay.selection.y
                const width = overlay.selection.width
                const height = overlay.selection.height
                const radius = Math.min(8, width / 2, height / 2)
                context.moveTo(x + radius, y)
                context.lineTo(x + width - radius, y)
                context.quadraticCurveTo(x + width, y, x + width, y + radius)
                context.lineTo(x + width, y + height - radius)
                context.quadraticCurveTo(x + width, y + height,
                                         x + width - radius, y + height)
                context.lineTo(x + radius, y + height)
                context.quadraticCurveTo(x, y + height, x, y + height - radius)
                context.lineTo(x, y + radius)
                context.quadraticCurveTo(x, y, x + radius, y)
                context.closePath()
            } else {
                context.rect(overlay.selection.x, overlay.selection.y,
                             overlay.selection.width, overlay.selection.height)
            }
            context.fill()
            context.globalCompositeOperation = "source-over"
        }
    }

    Rectangle {
        x: overlay.hoveredWindow ? overlay.hoveredWindow.x : 0
        y: overlay.hoveredWindow ? overlay.hoveredWindow.y : 0
        width: overlay.hoveredWindow ? overlay.hoveredWindow.width : 0
        height: overlay.hoveredWindow ? overlay.hoveredWindow.height : 0
        color: "transparent"
        border.color: overlay.accent
        border.width: 2
        radius: 8
        visible: overlay.hoveredWindow !== null && !overlay.dragging
        z: 2
    }

    Rectangle {
        x: overlay.selection.x
        y: overlay.selection.y
        width: overlay.selection.width
        height: overlay.selection.height
        color: "transparent"
        border.color: overlay.accent
        border.width: 2
        radius: overlay.selectedWindow ? 8 : 0
        visible: width > 1 && height > 1
        z: 3
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        width: hint.implicitWidth + 32
        height: 40
        radius: 12
        color: overlay.background
        opacity: 0.96
        z: 4

        Text {
            id: hint
            anchors.centerIn: parent
            text: overlay.statusText
            color: overlay.accent
            font.family: "Google Sans Flex"
            font.pointSize: 10
        }
    }

    MouseArea {
        id: selector
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.CrossCursor
        acceptedButtons: Qt.LeftButton
        z: 5
        onPressed: mouse => {
            const candidate = overlay.windowAt(mouse.x, mouse.y)
            if (overlay.containsSelection(mouse.x, mouse.y)
                    && (!candidate || (overlay.selectedWindow
                        && candidate.windowId === overlay.selectedWindow.windowId))) {
                overlay.drawing = true
                overlay.strokes = overlay.strokes.concat([{
                    "color": overlay.accent,
                    "width": overlay.strokeWidth,
                    "points": [{ "x": mouse.x, "y": mouse.y }]
                }])
                drawingCanvas.requestPaint()
                return
            }
            overlay.drawing = false
            overlay.startX = mouse.x
            overlay.startY = mouse.y
            overlay.pressedWindow = candidate
            overlay.hoveredWindow = overlay.pressedWindow
            overlay.selectedWindow = null
            overlay.dragging = true
            overlay.strokes = []
            overlay.selection = Qt.rect(mouse.x, mouse.y, 0, 0)
        }
        onPositionChanged: mouse => {
            if (overlay.drawing) {
                overlay.addStrokePoint(mouse.x, mouse.y)
                return
            }
            if (!overlay.dragging) {
                const candidate = overlay.windowAt(mouse.x, mouse.y)
                const candidateId = candidate ? candidate.windowId : ""
                const currentId = overlay.hoveredWindow
                    ? overlay.hoveredWindow.windowId : ""
                if (candidateId !== currentId)
                    overlay.hoveredWindow = candidate
                return
            }
            if (!overlay.dragging)
                return
            overlay.updateDrag(mouse.x, mouse.y)
        }
        onReleased: mouse => {
            if (overlay.drawing) {
                overlay.addStrokePoint(mouse.x, mouse.y)
                overlay.drawing = false
                keyScope.forceActiveFocus()
                return
            }
            if (!overlay.dragging)
                return
            overlay.updateDrag(mouse.x, mouse.y)
            overlay.dragging = false
            if (overlay.pressedWindow
                    && Math.max(Math.abs(mouse.x - overlay.startX),
                                Math.abs(mouse.y - overlay.startY)) < 5) {
                overlay.selectWindow(overlay.pressedWindow)
            } else {
                overlay.hoveredWindow = null
                overlay.selectedWindow = null
            }
            overlay.pressedWindow = null
            keyScope.forceActiveFocus()
        }
    }

    Item {
        id: keyScope
        anchors.fill: parent
        focus: true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape) {
                captureController.closeOverlay()
                event.accepted = true
            } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_C) {
                captureController.copySelection(Math.round(selection.x), Math.round(selection.y),
                                                Math.round(selection.width), Math.round(selection.height),
                                                "rectangle", strokes)
                event.accepted = true
            } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Z) {
                overlay.strokes = overlay.strokes.slice(0, -1)
                drawingCanvas.requestPaint()
                event.accepted = true
            } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                overlay.strokes = []
                drawingCanvas.requestPaint()
                event.accepted = true
            } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_S) {
                captureController.hideOverlay()
                captureController.saveSelection(Math.round(selection.x), Math.round(selection.y),
                                                Math.round(selection.width), Math.round(selection.height),
                                                "rectangle", strokes)
                event.accepted = true
            }
        }
    }

    Connections {
        target: captureController
        function onOperationFinished(success, message) {
            if (success) {
                captureController.closeOverlay()
            } else {
                captureController.showOverlay()
                if (message.length > 0)
                    overlay.statusText = message
            }
        }
    }

    Component.onCompleted: {
        keyScope.forceActiveFocus()
    }

}
