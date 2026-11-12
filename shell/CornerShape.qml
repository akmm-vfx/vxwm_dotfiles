import QtQuick
import QtQuick.Shapes

Item {
    id: connector

    property string corner: "top-left"
    property color fillColor: "#151218"
    property int radius: 10

    readonly property bool onLeft: corner.endsWith("left")
    readonly property bool onTop: corner.startsWith("top")

    width: radius
    height: radius

    Shape {
        anchors.fill: parent
        antialiasing: true

        transform: Scale {
            origin.x: connector.radius / 2
            origin.y: connector.radius / 2
            xScale: connector.onLeft ? 1 : -1
            yScale: connector.onTop ? 1 : -1
        }

        ShapePath {
            startX: 0
            startY: 0
            fillColor: connector.fillColor
            strokeColor: "transparent"

            PathLine { x: connector.radius; y: 0 }
            PathArc {
                x: 0
                y: connector.radius
                radiusX: connector.radius
                radiusY: connector.radius
                direction: PathArc.Counterclockwise
                useLargeArc: false
            }
            PathLine { x: 0; y: 0 }
        }
    }
}
