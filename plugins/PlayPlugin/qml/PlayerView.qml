// PlayerView.qml —— PlayPlugin 模块内部组件
// 只依赖 PlayPlugin 1.0、AppLog 1.0 和 Qt 标准模块，对宿主模块零感知。

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import AppLog    1.0          // Log 单例（基础设施层，所有插件共用）
import PlayPlugin 1.0          // PlayerEngine、PlaylistModel、MediaInfo、PlaybackContext

Item {
    id: root

    PlayerEngine {
        id: engine
        onErrorOccurred: (msg) => {
            errorBar.show(msg)
            Log.error("[PlayPlugin] PlayerEngine: " + msg)
        }
        onPlaybackStateChanged: (state) => {
            Log.info("[PlayPlugin] State → " + state)
            
        }
    }

    // 在 videoSurface 创建完成后，把它传给 engine 建立 frameReady 连接
    Component.onCompleted: engine.setSurface(videoSurface)

    PlaylistModel {
        id: playlist
        onCurrentMediaRequested: (url) => engine.open(url)
    }

    property alias playlistModel: playlist

    FileDialog {
        id: fileDlg
        title: "Open Media File"
        nameFilters: ["Video files (*.mp4 *.mkv *.avi *.mov *.flv *.webm)",
                      "Audio files (*.mp3 *.flac *.aac *.ogg *.wav)",
                      "All files (*)"]
        onAccepted: {
            playlist.append(selectedFile)
            playlist.currentIndex = playlist.count - 1
        }
    }

    // ── 视频输出 ──────────────────────────────────────────────────────────
    FFmpegSurface {
        id: videoSurface
        anchors { top: parent.top; left: parent.left; right: parent.right; bottom: controlBar.top }
        aspectRatioMode: Qt.KeepAspectRatio

        // 无媒体时的占位提示
        Rectangle {
            anchors.fill: parent
            color: "#08080e"
            visible: engine.playbackState === 0 && engine.currentMedia === null

            Column {
                anchors.centerIn: parent
                spacing: 18
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "▶"; font.pixelSize: 64; color: "#ffffff18" }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Open a file to start playing"; color: "#ffffff40"; font.pixelSize: 15 }
            }
        }

        // 标题（暂停时显示）
        Text {
            anchors { top: parent.top; left: parent.left; margins: 20 }
            text: engine.currentMedia ? engine.currentMedia.title : ""
            color: "#ffffffcc"; font.pixelSize: 16; font.bold: true
            opacity: engine.playbackState === 1 ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: 600 } }
        }

        MouseArea {
            anchors.fill: parent
            onDoubleClicked: Log.info("[PlayPlugin] Fullscreen toggle (TODO)")
        }
    }

    // ── 错误通知栏 ────────────────────────────────────────────────────────
    Rectangle {
        id: errorBar
        anchors { left: parent.left; right: parent.right; bottom: controlBar.top }
        height: 0; clip: true; color: "#7a1020"
        property string message: ""
        function show(msg) { message = msg; height = 36; hideTimer.restart() }
        Timer { id: hideTimer; interval: 4000; onTriggered: errorBar.height = 0 }
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        Text {
            anchors { verticalCenter: parent.verticalCenter; left: parent.left; leftMargin: 16 }
            text: "⚠  " + errorBar.message; color: "#ffd0d0"; font.pixelSize: 13
        }
    }

    // ── 控制栏 ────────────────────────────────────────────────────────────
    ControlBar {
        id: controlBar
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        playbackState: engine.playbackState
        position:      engine.position
        duration:      engine.duration
        volume:        engine.volume
        muted:         engine.muted
        onPlayPauseRequested: engine.togglePlayPause()
        onStopRequested:      engine.stop()
        onSeekRequested:      (ms) => engine.seek(ms)
        onVolumeRequested:    (v)  => engine.volume = v
        onMuteRequested:      engine.muted = !engine.muted
        onOpenRequested:      fileDlg.open()
        onPreviousRequested:  playlist.previous()
        onNextRequested:      playlist.next()
    }
}
