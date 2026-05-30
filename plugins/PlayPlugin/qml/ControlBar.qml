import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QuickUI.Components 1.0

Rectangle {
    id: root

    height: 82
    color: "#111319f2"

    property int playbackState: 0
    property real position: 0
    property real duration: 0
    property real volume: 1.0
    property bool muted: false
    property bool playlistOpen: false
    readonly property bool showPlaylistButton: true

    signal playPauseRequested()
    signal stopRequested()
    signal seekRequested(real ms)
    signal volumeRequested(real v)
    signal muteRequested()
    signal openRequested()
    signal previousRequested()
    signal nextRequested()
    signal playlistRequested()

    Rectangle {
        anchors.top: parent.top
        width: parent.width
        height: 1
        color: "#ffffff10"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 10

            Text {
                text: formatTime(root.position)
                color: "#aeb6c2"
                font.pixelSize: 11
                font.family: "Courier New"
                Layout.preferredWidth: 48
            }

            Slider {
                id: seekBar
                Layout.fillWidth: true
                from: 0.0
                to: 1.0
                value: seekBar.pressed ? seekBar.value
                                      : (root.duration > 0 ? root.position / root.duration : 0)
                enabled: root.duration > 0
                onMoved: root.seekRequested(value * root.duration)

                background: Rectangle {
                    x: seekBar.leftPadding
                    y: seekBar.topPadding + seekBar.availableHeight / 2 - height / 2
                    width: seekBar.availableWidth
                    height: 4
                    radius: 2
                    color: "#2c3039"

                    Rectangle {
                        width: seekBar.visualPosition * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: seekBar.enabled ? "#79a8ff" : "#4a4f5d"
                    }
                }

                handle: Rectangle {
                    x: seekBar.leftPadding + seekBar.visualPosition * (seekBar.availableWidth - width)
                    y: seekBar.topPadding + seekBar.availableHeight / 2 - height / 2
                    width: seekBar.pressed ? 16 : 12
                    height: width
                    radius: width / 2
                    color: seekBar.enabled ? "#dbe8ff" : "#6a707c"
                    border.color: "#79a8ff"
                    visible: seekBar.enabled

                    Behavior on width { NumberAnimation { duration: 100 } }
                }
            }

            Text {
                text: formatTime(root.duration)
                color: "#aeb6c2"
                font.pixelSize: 11
                font.family: "Courier New"
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 58
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            IconButton { iconText: "＋"; tooltip: "Open file"; fontSize: 16; onClicked: root.openRequested() }
            Item { width: 8 }
            IconButton { iconText: "⏮"; tooltip: "Previous"; fontSize: 15; onClicked: root.previousRequested() }

            IconButton {
                iconText: root.playbackState === 1 ? "⏸" : "▶"
                tooltip: root.playbackState === 1 ? "Pause" : "Play"
                fontSize: 20
                onClicked: root.playPauseRequested()
            }

            IconButton { iconText: "⏹"; tooltip: "Stop"; fontSize: 15; onClicked: root.stopRequested() }
            IconButton { iconText: "⏭"; tooltip: "Next"; fontSize: 15; onClicked: root.nextRequested() }

            Item { Layout.fillWidth: true }

            IconButton {
                iconText: root.muted ? "◖" : "◕"
                tooltip: root.muted ? "Unmute" : "Mute"
                fontSize: 15
                onClicked: root.muteRequested()
            }

            Slider {
                id: volumeSlider
                from: 0.0
                to: 1.0
                value: root.muted ? 0 : root.volume
                Layout.preferredWidth: 96
                Layout.alignment: Qt.AlignVCenter
                onMoved: root.volumeRequested(value)

                background: Rectangle {
                    x: volumeSlider.leftPadding
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    width: volumeSlider.availableWidth
                    height: 4
                    radius: 2
                    color: "#2c3039"

                    Rectangle {
                        width: volumeSlider.visualPosition * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: "#79a8ff"
                    }
                }

                handle: Rectangle {
                    x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    width: 12
                    height: 12
                    radius: 6
                    color: "#dbe8ff"
                    border.color: "#79a8ff"
                }
            }

            IconButton {
                visible: root.showPlaylistButton
                iconText: root.playlistOpen ? "☰" : "☷"
                tooltip: root.playlistOpen ? "Hide playlist" : "Show playlist"
                fontSize: 16
                onClicked: root.playlistRequested()
            }
        }
    }

    function formatTime(ms) {
        if (ms <= 0 || isNaN(ms)) return "0:00"
        const s = Math.floor(ms / 1000)
        const h = Math.floor(s / 3600)
        const m = Math.floor((s % 3600) / 60)
        const sec = s % 60
        const mm = m.toString().padStart(2, "0")
        const ss = sec.toString().padStart(2, "0")
        return h > 0 ? (h + ":" + mm + ":" + ss) : (m + ":" + ss)
    }
}
