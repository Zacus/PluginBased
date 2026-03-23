import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VideoPlayer 1.0
import QuickUI.Components 1.0

ApplicationWindow {
    id: root
    title: AppController.appName + " " + AppController.appVersion
    width: 1280; height: 780
    minimumWidth: 800; minimumHeight: 520
    visible: true
    color: "#0f0f13"

    Connections {
        target: AppController
        function onPluginsReadyChanged() {
            if (AppController.pluginsReady)
                AppController.logInfo("QML: plugins ready — count=" + PluginManager.pluginCount)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── 顶部工具栏 ────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 42; color: "#18181f"

            RowLayout {
                anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
                spacing: 12

                // 返回首页按钮（仅在非主页时显示）
                IconButton {
                    iconText: "←"
                    tooltip:  "返回主页"
                    visible:  stack.depth > 1
                    onClicked: stack.pop()
                }

                Text {
                    text: "▶ " + AppController.appName
                    color: "#e8e8f0"; font.pixelSize: 15; font.bold: true; font.letterSpacing: 1
                }

                // 当前页面副标题（非主页时显示）
                Text {
                    visible: stack.depth > 1
                    text: stack.currentItem ? (stack.currentItem.pageTitle || "") : ""
                    color: "#55556a"; font.pixelSize: 13
                }

                Item { Layout.fillWidth: true }

                // 插件状态徽章
                Rectangle {
                    width: badgeRow.implicitWidth + 20; height: 24; radius: 12
                    color: AppController.pluginsReady ? "#1a3a2a" : "#2a1a1a"
                    border.color: AppController.pluginsReady ? "#2ecc71" : "#e74c3c"
                    border.width: 1

                    Row {
                        id: badgeRow
                        anchors.centerIn: parent; spacing: 6
                        Rectangle {
                            width: 7; height: 7; radius: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: AppController.pluginsReady ? "#2ecc71" : "#e74c3c"
                        }
                        Text {
                            text: AppController.pluginsReady
                                  ? (PluginManager.pluginCount + " plugin(s) loaded")
                                  : "loading plugins…"
                            color: "#c0c0d0"; font.pixelSize: 11
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }

                IconButton { iconText: "✕"; tooltip: "Quit"; onClicked: AppController.quit() }
            }

            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: "#ffffff12" }
        }

        // ── 页面栈 ────────────────────────────────────────────────────────
        StackView {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true

            pushEnter: Transition {
                PropertyAnimation { property: "x"; from: stack.width * 0.04; to: 0; duration: 220; easing.type: Easing.OutCubic }
                PropertyAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
            }
            pushExit: Transition {
                PropertyAnimation { property: "opacity"; from: 1; to: 0; duration: 150 }
            }
            popEnter: Transition {
                PropertyAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
            }
            popExit: Transition {
                PropertyAnimation { property: "x"; from: 0; to: stack.width * 0.04; duration: 200; easing.type: Easing.InCubic }
                PropertyAnimation { property: "opacity"; from: 1; to: 0; duration: 150 }
            }

            initialItem: HomePanel {
                property string pageTitle: ""

                onBuiltinCardClicked: function(viewId) {
                    if (viewId === "player" || viewId === "playlist") {
                        AppController.logInfo("HomePanel: open builtin view=" + viewId)
                        stack.push(playerViewComponent)
                    }
                }
                onPluginCardClicked: function(pluginId) {
                    AppController.logInfo("HomePanel: open plugin=" + pluginId)
                }
            }
        }
    }

    // ── 播放器页面 ────────────────────────────────────────────────────────────
    Component {
        id: playerViewComponent

        Item {
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
    }
}
