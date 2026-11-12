import QtQuick
import QtQuick.Shapes

Item {
    id: panel

    property int panelWidth: 48
    property int panelHeight: 306
    property int cornerRadius: 8
    property int connectorSize: 8
    property color backgroundColor: "#151218"

    width: panelWidth
    height: panelHeight + 2 * connectorSize

    Shape {
        x: 0
        y: panel.connectorSize
        width: panel.panelWidth
        height: panel.panelHeight
        antialiasing: true

        ShapePath {
            fillColor: panel.backgroundColor
            strokeColor: "transparent"
            startX: panel.panelWidth
            startY: 0

            PathLine { x: panel.cornerRadius; y: 0 }
            PathCubic {
                control1X: panel.cornerRadius * 0.448
                control1Y: 0
                control2X: 0
                control2Y: panel.cornerRadius * 0.448
                x: 0
                y: panel.cornerRadius
            }
            PathLine { x: 0; y: panel.panelHeight - panel.cornerRadius }
            PathCubic {
                control1X: 0
                control1Y: panel.panelHeight - panel.cornerRadius * 0.448
                control2X: panel.cornerRadius * 0.448
                control2Y: panel.panelHeight
                x: panel.cornerRadius
                y: panel.panelHeight
            }
            PathLine { x: panel.panelWidth; y: panel.panelHeight }
            PathLine { x: panel.panelWidth; y: 0 }
        }
    }

    CornerShape {
        x: panel.panelWidth - panel.connectorSize * 1.5
        y: 0
        rotation: 90
        corner: "top-right"
        fillColor: panel.backgroundColor
        radius: panel.connectorSize
    }

    CornerShape {
        x: panel.panelWidth - panel.connectorSize * 1.5
        y: panel.connectorSize + panel.panelHeight
        rotation: 360
        corner: "top-right"
        fillColor: panel.backgroundColor
        radius: panel.connectorSize
    }
}
