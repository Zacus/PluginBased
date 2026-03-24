// PlayPluginView.qml
// 播放器插件的根页面，原 main.qml 中 playerViewComponent 的内容移至此处。
// 宿主通过 Loader { source: plugin.qmlComponentUrl() } 加载本文件。

import QtQuick
import QtQuick.Layouts
import VideoPlayer 1.0
import QuickUI.Components 1.0

Item {
    id: root

    // 宿主可读取此属性作为顶部工具栏副标题
    property string pageTitle: "视频播放器"

    RowLayout {
        anchors.fill: parent
        spacing: 0

        PlayerView {
            id: playerView
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        Rectangle {
            width: 1
            Layout.fillHeight: true
            color: "#ffffff10"
        }

        PlaylistView {
            width: 280
            Layout.fillHeight: true
            playlistModel: playerView.playlistModel
        }
    }
}
