pragma Singleton
import QtQuick
import KettleLAN.Core

// Kolory i wymiary interfejsu – jedno miejsce do zmiany wyglądu całego programu.
QtObject {
    readonly property bool dark: Backend.darkMode

    readonly property color bg: dark ? "#14161A" : "#F4F5F7"
    readonly property color surface: dark ? "#1C1F25" : "#FFFFFF"
    readonly property color surface2: dark ? "#272B34" : "#EBEDF1"
    readonly property color border: dark ? "#2E333D" : "#DDE1E7"
    readonly property color text: dark ? "#E9ECF1" : "#15181D"
    readonly property color muted: dark ? "#A2AAB8" : "#545D6B"
    readonly property color ok: dark ? "#4CC38A" : "#1F8A57"

    readonly property color accent: "#F08A4B"
    readonly property color accentHover: "#F49B63"
    readonly property color accentPressed: "#DC7A3D"
    readonly property color accentText: dark ? "#F5A877" : "#B4531F"
    readonly property color accentInk: "#1A1208"

    readonly property color directBg: dark ? "#1E3A2D" : "#DDF3E8"
    readonly property color directFg: dark ? "#7FDCAE" : "#14613D"
    readonly property color relayBg: dark ? "#3A2F17" : "#FBEFD2"
    readonly property color relayFg: dark ? "#F2C96B" : "#7A5508"

    readonly property int radius: 12
    readonly property int smallRadius: 8
    readonly property int pad: 20
    readonly property string mono: Qt.platform.os === "windows" ? "Consolas" : "monospace"
}
