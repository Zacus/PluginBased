// PlayPluginView.qml —— PlayPlugin 插件 QML 入口
// 宿主通过 Loader { source: plugin.qmlComponentUrl() } 加载本文件。
// 依赖 PlayPlugin 1.0、Qt 标准模块，以及宿主提供的 QuickUI.Components。

import QtQuick
import PluginBased 1.0
import PlayPlugin 1.0
import QuickUI.Components 1.0

Item {
    id: root

    property string pageTitle: AppController.currentLanguage, qsTr("Video Player")
    property bool playlistOpen: false
    readonly property int drawerWidth: Math.min(340, Math.max(292, Math.round(width * 0.28)))

    Rectangle {
        anchors.fill: parent
        color: ComponentTheme.surface
    }

    PlayerView {
        id: playerView
        anchors.fill: parent
        playlistOpen: root.playlistOpen
        onPlaylistToggleRequested: root.playlistOpen = !root.playlistOpen
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: root.playlistOpen ? 0.24 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: ComponentTheme.durationFast; easing.type: Easing.OutCubic } }

        MouseArea {
            anchors.fill: parent
            enabled: root.playlistOpen
            onClicked: root.playlistOpen = false
        }
    }

    Item {
        id: playlistDrawer
        width: root.drawerWidth
        height: parent.height
        x: root.playlistOpen ? parent.width - width : parent.width + 8
        y: 0
        clip: true

        Behavior on x {
            NumberAnimation { duration: ComponentTheme.durationNormal; easing.type: Easing.OutCubic }
        }

        Rectangle {
            anchors.fill: parent
            color: ComponentTheme.surface
            border.color: ComponentTheme.separator
        }

        PlaylistView {
            anchors.fill: parent
            playlistModel: playerView.playlistModel
            onCloseRequested: root.playlistOpen = false
        }
    }
}
