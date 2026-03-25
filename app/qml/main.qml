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

                onPluginCardClicked: function(pluginId, pluginIndex) {
                    AppController.logInfo("HomePanel: open plugin=" + pluginId + " index=" + pluginIndex)
                    if (PluginManager.pluginHasQmlUI(pluginIndex)) {
                        // 插件提供自己的 QML 页面，通过 Loader 动态加载
                        stack.push(pluginPageComponent, {
                            "pluginQmlUrl":  PluginManager.pluginQmlUrl(pluginIndex),
                            "pluginTitle":   PluginManager.pluginCardName(pluginIndex)
                        })
                    } else {
                        // 无 QML UI 的插件，仅记录日志
                        AppController.logWarn("HomePanel: plugin has no QML UI, index=" + pluginIndex)
                    }
                }
            }
        }
    }

    // ── 插件页面（通用 Loader 容器，供 pluginCardClicked 使用）─────────────
    Component {
        id: pluginPageComponent

        Item {
            // 由 stack.push() 注入
            property url    pluginQmlUrl: ""
            property string pluginTitle:  ""
            property string pageTitle:    pluginTitle

            Loader {
                id: pluginLoader
                anchors.fill: parent
                source: pluginQmlUrl

                onStatusChanged: {
                    if (status === Loader.Error)
                        AppController.logError("pluginPageComponent: failed to load " + pluginQmlUrl)
                    else if (status === Loader.Ready)
                        AppController.logInfo("pluginPageComponent: loaded " + pluginQmlUrl)
                }
            }

            // 加载中占位
            Rectangle {
                anchors.fill: parent
                color: "#0f0f13"
                visible: pluginLoader.status === Loader.Loading

                Column {
                    anchors.centerIn: parent
                    spacing: 14
                    BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: true }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "正在加载插件界面…"
                        color: "#50507a"; font.pixelSize: 13
                    }
                }
            }

            // 加载失败占位
            Rectangle {
                anchors.fill: parent
                color: "#0f0f13"
                visible: pluginLoader.status === Loader.Error

                Column {
                    anchors.centerIn: parent
                    spacing: 10
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "⚠"; font.pixelSize: 40; color: "#e74c3c"
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "插件界面加载失败"
                        color: "#c0c0d0"; font.pixelSize: 14
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: pluginQmlUrl.toString()
                        color: "#50506a"; font.pixelSize: 11
                    }
                }
            }
        }
    }
}
