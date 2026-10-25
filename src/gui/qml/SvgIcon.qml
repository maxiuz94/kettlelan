import QtQuick
import QtQuick.Shapes
import KettleLAN

// Line icon drawn from an SVG path (24×24 grid), no image files.
Item {
    id: root
    property string name
    property color color: Theme.text
    property real size: 20
    property real strokeWidth: 1.8

    implicitWidth: size
    implicitHeight: size
    layer.enabled: true
    layer.samples: 4

    readonly property var paths: ({
        "kettle": "M8.2 9h7.6c2.8 1.6 4.6 5.2 4.6 8.6c0 2.3-1.1 3.4-3.4 3.4H7c-2.3 0-3.4-1.1-3.4-3.4c0-3.4 1.8-7 4.6-8.6z M5.6 12.2L1.8 8.6 M7.6 9.4c-.2-6.4 9-6.4 8.8 0 M12 7.4v.1 M2.4 5.6c-.8-1 .8-1.8 0-3",
        "settings": "M12 9a3 3 0 1 0 0 6a3 3 0 1 0 0-6z M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z",
        "copy": "M11 9h7a2 2 0 0 1 2 2v7a2 2 0 0 1-2 2h-7a2 2 0 0 1-2-2v-7a2 2 0 0 1 2-2z M5 15V5a2 2 0 0 1 2-2h10",
        "back": "M15 18l-6-6 6-6",
        "plus": "M12 5v14 M5 12h14",
        "userPlus": "M16 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2 M9 3a4 4 0 1 0 0 8a4 4 0 1 0 0-8z M19 8v6 M22 11h-6",
        "leave": "M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4 M16 17l5-5-5-5 M21 12H9",
        "alert": "M12 9v4 M12 17h.01 M10.3 3.9L1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z",
        "check": "M20 6L9 17l-5-5",
        "trash": "M3 6h18 M8 6V4h8v2 M19 6l-1 14H6L5 6 M10 11v6 M14 11v6",
        "edit": "M12 20h9 M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4z",
        "stop": "M6 6h12v12H6z",
        "play": "M7 5l12 7-12 7z"
    })

    Shape {
        width: 24
        height: 24
        scale: root.size / 24
        transformOrigin: Item.TopLeft
        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.paths[root.name] || "" }
        }
    }
}
