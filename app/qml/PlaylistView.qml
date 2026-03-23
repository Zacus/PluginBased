import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VideoPlayer.UI 1.0

Item {
    id: root

    // 由父级 PlayerView 注入
    property var playlistModel: null

    Rectangle {
        anchors.fill: parent
        color: "#0e0e15"

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // ── 标题栏 ────────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                height: 42
                color: "#16161e"

                RowLayout {
                    anchors { fill: parent; leftMargin: 14; rightMargin: 10 }
                    Text {
                        text: "PLAYLIST"
                        color: "#6060a0"; font.pixelSize: 11; font.bold: true; font.letterSpacing: 2
                    }
                    Item { Layout.fillWidth: true }
                    IconButton {
                        iconText: "🗑"; tooltip: "Clear playlist"; fontSize: 13
                        onClicked: { if (root.playlistModel) root.playlistModel.clear() }
                    }
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width; height: 1; color: "#ffffff0e"
                }
            }

            // ── 列表 ──────────────────────────────────────────────────────
            ListView {
                id: listView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.playlistModel

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Text {
                    anchors.centerIn: parent
                    visible: listView.count === 0
                    text: "No items\nUse 📂 to open files"
                    horizontalAlignment: Text.AlignHCenter
                    color: "#ffffff25"; font.pixelSize: 13; lineHeight: 1.6
                }

                delegate: Rectangle {
                    width: listView.width
                    height: 52
                    color: model.isCurrent ? "#1e1e35" : (delegateMouse.containsMouse ? "#17171f" : "transparent")

                    Rectangle {
                        width: 3; height: parent.height
                        color: model.isCurrent ? "#7c6fff" : "transparent"
                        radius: 1
                    }

                    RowLayout {
                        anchors { fill: parent; leftMargin: 14; rightMargin: 8 }
                        spacing: 10

                        Text {
                            text: model.isCurrent ? "▶" : (index + 1).toString()
                            color: model.isCurrent ? "#7c6fff" : "#505070"
                            font.pixelSize: model.isCurrent ? 14 : 11
                            width: 20; horizontalAlignment: Text.AlignHCenter
                        }
                        Text {
                            Layout.fillWidth: true
                            text: model.title
                            color: model.isCurrent ? "#e0e0f8" : "#a0a0c0"
                            font.pixelSize: 13; elide: Text.ElideRight
                        }
                        IconButton {
                            iconText: "✕"; fontSize: 11; visible: delegateMouse.containsMouse; tooltip: "Remove"
                            onClicked: { if (root.playlistModel) root.playlistModel.remove(index) }
                        }
                    }

                    MouseArea {
                        id: delegateMouse
                        anchors.fill: parent; hoverEnabled: true
                        onDoubleClicked: { if (root.playlistModel) root.playlistModel.setCurrentIndex(index) }
                    }
                }
            }

            // ── 底部提示 ──────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                height: 36; color: "#16161e"
                Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: "#ffffff0e" }
                Text {
                    anchors.centerIn: parent
                    text: "Double-click to play  ·  ✕ to remove"
                    color: "#ffffff25"; font.pixelSize: 11
                }
            }
        }
    }
}
