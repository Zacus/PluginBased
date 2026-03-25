// PlayerView.qml —— PlayPlugin 模块内部组件
// PlayerEngine / PlaylistModel / MediaInfo 均注册在 "PlayPlugin 1.0"，
// ControlBar 与本文件同属一个 qrc 前缀，由 Qt 自动发现，无需 import。

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import PlayPlugin 1.0          // PlayerEngine、PlaylistModel、MediaInfo
import VideoPlayer 1.0         // AppController（宿主基础设施，跨模块访问）

Item {
    id: root

    PlayerEngine {
        id: engine
        onErrorOccurred: (msg) => {
            errorBar.show(msg)
            AppController.logError("PlayerEngine: " + msg)
        }
        onPlaybackStateChanged: (state) => {
            AppController.logInfo("State → " + state)
        }
    }

    PlaylistModel {
        id: playlist
        onCurrentMediaRequested: (url) => engine.open(url)
    }

    // 暴露给外部（PlaylistView 通过属性绑定使用）
    property alias playlistModel: playlist

    FileDialog {
        id: fileDlg
        title: "Open Media File"
        nameFilters: ["Video files (*.mp4 *.mkv *.avi *.mov *.flv *.webm)",
                      "Audio files (*.mp3 *.flac *.aac *.ogg *.wav)",
                      "All files (*)"]
        onAccepted: {
            playlist.append(selectedFile)
            playlist.setCurrentIndex(playlist.count - 1)
        }
    }

    // ── 视频区域 ──────────────────────────────────────────────────────────
    Rectangle {
        id: videoArea
        anchors { top: parent.top; left: parent.left; right: parent.right; bottom: controlBar.top }
        color: "#08080e"

        Column {
            anchors.centerIn: parent
            spacing: 18
            visible: engine.playbackState === 0 && engine.currentMedia === null
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "▶"; font.pixelSize: 64; color: "#ffffff18" }
            Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Open a file to start playing"; color: "#ffffff40"; font.pixelSize: 15 }
        }

        Text {
            anchors { top: parent.top; left: parent.left; margins: 20 }
            text: engine.currentMedia ? engine.currentMedia.title : ""
            color: "#ffffffcc"; font.pixelSize: 16; font.bold: true
            opacity: engine.playbackState === 1 ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: 600 } }
        }

        MouseArea {
            anchors.fill: parent
            onDoubleClicked: AppController.logInfo("Fullscreen toggle (TODO)")
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
