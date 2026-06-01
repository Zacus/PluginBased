import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QuickUI.Components 1.0

Item {
    id: root

    property var playlistModel: null
    signal closeRequested()

    Rectangle {
        anchors.fill: parent
        color: "#15171d"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 58
            color: "#181b22"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 12
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Text {
                        text: "播放列表"
                        color: "#f4f7fb"
                        font.pixelSize: 15
                        font.bold: true
                    }

                    Text {
                        text: listView.count + " 个项目"
                        color: "#8f98a8"
                        font.pixelSize: 11
                    }
                }

                IconButton {
                    iconText: "⌫"
                    tooltip: "Clear playlist"
                    fontSize: 14
                    enabled: listView.count > 0
                    onClicked: {
                        if (root.playlistModel)
                            root.playlistModel.clear()
                    }
                }

                IconButton {
                    iconText: "×"
                    tooltip: "Close playlist"
                    fontSize: 17
                    onClicked: root.closeRequested()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: "#ffffff10"
            }
        }

        ListView {
            id: listView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.playlistModel
            boundsBehavior: Flickable.StopAtBounds

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Text {
                anchors.centerIn: parent
                width: parent.width - 48
                visible: listView.count === 0
                text: "暂无播放项目\n点击左下角 ＋ 添加媒体文件"
                horizontalAlignment: Text.AlignHCenter
                color: "#8f98a870"
                font.pixelSize: 13
                lineHeight: 1.5
            }

            delegate: Rectangle {
                id: row
                width: listView.width
                height: 58
                color: model.isCurrent ? "#202936" : (delegateHover.hovered ? "#1b2029" : "transparent")

                HoverHandler {
                    id: delegateHover
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onDoubleTapped: {
                        if (root.playlistModel)
                            root.playlistModel.currentIndex = index
                    }
                }

                Rectangle {
                    x: 0
                    y: 10
                    width: 3
                    height: parent.height - 20
                    radius: 2
                    color: model.isCurrent ? "#79a8ff" : "transparent"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 10
                    spacing: 10

                    Text {
                        text: model.isCurrent ? "▶" : (index + 1).toString()
                        color: model.isCurrent ? "#9fc1ff" : "#626b79"
                        font.pixelSize: model.isCurrent ? 13 : 11
                        Layout.preferredWidth: 24
                        horizontalAlignment: Text.AlignHCenter
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: model.title
                            color: model.isCurrent ? "#f4f7fb" : "#c6ccd6"
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: model.url
                            color: "#77808e"
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                        }
                    }

                    IconButton {
                        iconText: "×"
                        fontSize: 13
                        visible: delegateHover.hovered
                        tooltip: "Remove"
                        onClicked: {
                            if (root.playlistModel)
                                root.playlistModel.remove(index)
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 34
            color: "#181b22"

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: "#ffffff0f"
            }

            Text {
                anchors.centerIn: parent
                text: "双击播放，悬停后可移除"
                color: "#8f98a870"
                font.pixelSize: 11
            }
        }
    }
}
