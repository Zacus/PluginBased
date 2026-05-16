import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QuickUI.Components 1.0

Rectangle {
    id: root
    height: 90
    color: "#13131a"

    // playbackState: 0=Stopped 1=Playing 2=Paused
    property int    playbackState: 0
    property real   position:  0
    property real   duration:  0
    property real   volume:    1.0
    property bool   muted:     false

    signal playPauseRequested()
    signal stopRequested()
    signal seekRequested(real ms)
    signal volumeRequested(real v)
    signal muteRequested()
    signal openRequested()
    signal previousRequested()
    signal nextRequested()

    Rectangle {
        anchors.top: parent.top
        width: parent.width; height: 1
        color: "#ffffff14"
    }

    ColumnLayout {
        anchors { fill: parent; margins: 12 }
        spacing: 8

        // ── 进度条 ────────────────────────────────────────────────────────
        Slider {
            id: seekBar
            Layout.fillWidth: true
            from:    0.0
            to:      1.0
            value:   seekBar.pressed ? seekBar.value
                             : (root.duration > 0 ? root.position / root.duration : 0)
            enabled: root.duration > 0

            onMoved: root.seekRequested(value * root.duration)

            background: Rectangle {
                x: seekBar.leftPadding
                y: seekBar.topPadding + seekBar.availableHeight / 2 - height / 2
                width:  seekBar.availableWidth
                height: 4
                radius: 2
                color: "#2a2a3a"
                Rectangle {
                    width:  seekBar.visualPosition * parent.width
                    height: parent.height
                    radius: parent.radius
                    color:  seekBar.enabled ? "#7c6fff" : "#44445a"
                }
            }
            handle: Rectangle {
                x: seekBar.leftPadding + seekBar.visualPosition * (seekBar.availableWidth - width)
                y: seekBar.topPadding  + seekBar.availableHeight / 2 - height / 2
                width: 14; height: 14; radius: 7
                color: seekBar.pressed ? "#9d90ff" : "#7c6fff"
                border.color: "#ffffff30"
                visible: seekBar.enabled
            }
        }

        // 时间标签
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: formatTime(root.position)
                color: "#8888a0"; font.pixelSize: 11; font.family: "Courier New"
            }
            Item { Layout.fillWidth: true }
            Text {
                text: formatTime(root.duration)
                color: "#8888a0"; font.pixelSize: 11; font.family: "Courier New"
            }
        }

        // ── 按钮行 ────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            IconButton { iconText: "📂"; tooltip: "Open file";  onClicked: root.openRequested() }
            Item { width: 8 }
            IconButton { iconText: "⏮"; tooltip: "Previous";   onClicked: root.previousRequested() }

            // 播放/暂停  (1 = Playing)
            IconButton {
                iconText: root.playbackState === 1 ? "⏸" : "▶"
                tooltip:  root.playbackState === 1 ? "Pause" : "Play"
                fontSize: 20
                onClicked: root.playPauseRequested()
            }

            IconButton { iconText: "⏹"; tooltip: "Stop"; onClicked: root.stopRequested() }
            IconButton { iconText: "⏭"; tooltip: "Next"; onClicked: root.nextRequested() }

            Item { Layout.fillWidth: true }

            IconButton {
                iconText: root.muted ? "🔇" : (root.volume > 0.5 ? "🔊" : "🔉")
                tooltip:  root.muted ? "Unmute" : "Mute"
                onClicked: root.muteRequested()
            }

            Slider {
                id: volumeSlider
                from: 0.0; to: 1.0
                value: root.muted ? 0 : root.volume
                width: 90
                Layout.alignment: Qt.AlignVCenter
                onMoved: root.volumeRequested(value)

                background: Rectangle {
                    x: volumeSlider.leftPadding
                    y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                    width: volumeSlider.availableWidth; height: 4; radius: 2
                    color: "#2a2a3a"
                    Rectangle {
                        width:  volumeSlider.visualPosition * parent.width
                        height: parent.height; radius: parent.radius; color: "#7c6fff"
                    }
                }
                handle: Rectangle {
                    x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                    y: volumeSlider.topPadding  + volumeSlider.availableHeight / 2 - height / 2
                    width: 14; height: 14; radius: 7
                    color: volumeSlider.pressed ? "#9d90ff" : "#7c6fff"
                    border.color: "#ffffff30"
                }
            }
        }
    }

    function formatTime(ms) {
        if (ms <= 0 || isNaN(ms)) return "0:00"
        const s = Math.floor(ms / 1000)
        const h = Math.floor(s / 3600)
        const m = Math.floor((s % 3600) / 60)
        const sec = s % 60
        const mm  = m.toString().padStart(2, "0")
        const ss  = sec.toString().padStart(2, "0")
        return h > 0 ? (h + ":" + mm + ":" + ss) : (m + ":" + ss)
    }
}
