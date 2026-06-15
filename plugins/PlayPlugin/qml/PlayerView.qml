// PlayerView.qml —— PlayPlugin 模块内部组件
// 依赖 PlayPlugin 1.0、AppLog 1.0、Qt 标准模块，以及子组件使用的 QuickUI.Components。

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import AppLog    1.0          // Log 单例（基础设施层，所有插件共用）
import PluginBased 1.0
import PlayPlugin 1.0          // PlayerEngine、PlaylistModel、MediaInfo、PlaybackContext
import QuickUI.Components 1.0

Item {
    id: root

    property bool playlistOpen: false
    signal playlistToggleRequested()

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
        title: AppController.currentLanguage, qsTr("Open Media File")
        nameFilters: AppController.currentLanguage,
                     [qsTr("Video files (*.mp4 *.mkv *.avi *.mov *.flv *.webm)"),
                      qsTr("Audio files (*.mp3 *.flac *.aac *.ogg *.wav)"),
                      qsTr("All files (*)")]
        onAccepted: {
            playlist.append(selectedFile)
            playlist.currentIndex = playlist.count - 1
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#0b0c10"
    }

    // ── 视频输出 ──────────────────────────────────────────────────────────
    FFmpegSurface {
        id: videoSurface
        anchors { top: parent.top; left: parent.left; right: parent.right; bottom: controlBar.top }
        aspectRatioMode: Qt.KeepAspectRatio

        // 无媒体时的占位提示
        Rectangle {
            anchors.fill: parent
            color: "#0b0c10"
            visible: engine.playbackState === 0 && engine.currentMedia === null

            Column {
                anchors.centerIn: parent
                spacing: 12
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "▶"
                    font.pixelSize: 52
                    color: ComponentTheme.textDisabled
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: AppController.currentLanguage, qsTr("Open a media file to start playback")
                    color: ComponentTheme.textSecondary
                    font.pixelSize: 14
                }
            }
        }

        // 标题（暂停时显示）
        Text {
            anchors { top: parent.top; left: parent.left; margins: 24 }
            text: engine.currentMedia ? engine.currentMedia.title : ""
            color: ComponentTheme.textPrimary
            font.pixelSize: 15
            font.bold: true
            opacity: engine.playbackState === 1 ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: ComponentTheme.durationNormal } }
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
        Behavior on height { NumberAnimation { duration: ComponentTheme.durationNormal; easing.type: Easing.OutQuad } }
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
        playlistOpen:  root.playlistOpen
        onPlayPauseRequested: engine.togglePlayPause()
        onStopRequested:      engine.stop()
        onSeekRequested:      (ms) => engine.seek(ms)
        onVolumeRequested:    (v)  => engine.volume = v
        onMuteRequested:      engine.muted = !engine.muted
        onOpenRequested:      fileDlg.open()
        onPreviousRequested:  playlist.previous()
        onNextRequested:      playlist.next()
        onPlaylistRequested:  root.playlistToggleRequested()
    }
}
