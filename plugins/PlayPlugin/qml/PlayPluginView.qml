// PlayPluginView.qml —— PlayPlugin 插件 QML 入口
// 宿主通过 Loader { source: plugin.qmlComponentUrl() } 加载本文件。
// 本文件及 PlayerView / ControlBar / PlaylistView 全部打包在
// 同一 qrc 前缀 /PlayPlugin/qml/ 下，属于完全自包含的插件 UI 层。

import QtQuick
import QtQuick.Layouts
import VideoPlayer 1.0         // AppController（宿主基础设施）
import PlayPlugin 1.0          // PlayerEngine、PlaylistModel 等插件 C++ 类型

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
