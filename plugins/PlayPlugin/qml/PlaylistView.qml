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
        color: ComponentTheme.surface
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            height: 58
            color: ComponentTheme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 12
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1

                    Text {
                        text: qsTr("Playlist")
                        color: ComponentTheme.textPrimary
                        font.pixelSize: 15
                        font.bold: true
                    }

                    Text {
                        text: qsTr("%1 item(s)").arg(listView.count)
                        color: ComponentTheme.textSecondary
                        font.pixelSize: 11
                    }
                }

                IconButton {
                    iconText: "⌫"
                    tooltip: qsTr("Clear playlist")
                    fontSize: 14
                    enabled: listView.count > 0
                    onClicked: {
                        if (root.playlistModel)
                            root.playlistModel.clear()
                    }
                }

                IconButton {
                    iconText: "×"
                    tooltip: qsTr("Close playlist")
                    fontSize: 17
                    onClicked: root.closeRequested()
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: ComponentTheme.separator
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
                text: qsTr("No playlist items\nClick + in the lower left to add media files")
                horizontalAlignment: Text.AlignHCenter
                color: ComponentTheme.textSecondary
                font.pixelSize: 13
                lineHeight: 1.5
            }

            delegate: Rectangle {
                id: row
                width: listView.width
                height: 58
                color: model.isCurrent ? ComponentTheme.buttonPressed
                                       : (delegateHover.hovered ? ComponentTheme.surfaceHover : "transparent")

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
                    color: model.isCurrent ? ComponentTheme.accent : "transparent"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 10
                    spacing: 10

                    Text {
                        text: model.isCurrent ? "▶" : (index + 1).toString()
                        color: model.isCurrent ? ComponentTheme.accent : ComponentTheme.textDisabled
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
                            color: model.isCurrent ? ComponentTheme.textPrimary : ComponentTheme.textSecondary
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }

                        Text {
                            Layout.fillWidth: true
                            text: model.url
                            color: ComponentTheme.textDisabled
                            font.pixelSize: 10
                            elide: Text.ElideMiddle
                        }
                    }

                    IconButton {
                        iconText: "×"
                        fontSize: 13
                        visible: delegateHover.hovered
                        tooltip: qsTr("Remove")
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
            color: ComponentTheme.surface

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: ComponentTheme.separator
            }

            Text {
                anchors.centerIn: parent
                text: qsTr("Double-click to play, hover to remove")
                color: ComponentTheme.textSecondary
                font.pixelSize: 11
            }
        }
    }
}
