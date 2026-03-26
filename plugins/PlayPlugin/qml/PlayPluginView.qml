// PlayPluginView.qml —— PlayPlugin 插件 QML 入口
// 宿主通过 Loader { source: plugin.qmlComponentUrl() } 加载本文件。
// 只依赖 PlayPlugin 1.0 和 Qt 标准模块，对宿主模块零感知。

import QtQuick
import QtQuick.Layouts
import PlayPlugin 1.0          // PlayerEngine、PlaylistModel、PlaybackContext 等

Item {
    id: root

    // 宿主读取此属性作为顶部工具栏副标题
    property string pageTitle: "视频播放器"

    RowLayout {
        anchors.fill: parent
        spacing: 0

        PlayerView {
            id: playerView
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        Rectangle { width: 1; Layout.fillHeight: true; color: "#ffffff10" }

        PlaylistView {
            width: 280
            Layout.fillHeight: true
            playlistModel: playerView.playlistModel
        }
    }
}
