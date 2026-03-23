import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import VideoPlayer 1.0

Item {
    id: root

    signal pluginCardClicked(string pluginId)
    signal builtinCardClicked(string viewId)

    // ── 内联组件：必须在根对象内部定义 ──────────────────────────────────────

    component PluginCard: Rectangle {
        id: card

        property string cardId:   ""
        property string iconText: "▶"
        property string iconBg:   "#221e50"
        property string cardName: ""
        property string cardDesc: ""
        property string cardTag:  ""
        property string tagBg:    "#1e1e35"
        property string tagFg:    "#7c6fff"

        signal clicked(string id)

        implicitHeight: cardInner.implicitHeight + 44
        height: implicitHeight

        color: hov.containsMouse ? "#1c1c28" : "#16161e"
        border.color: hov.containsMouse ? "#7c6fff33" : "#ffffff0e"
        border.width: 1
        radius: 14

        Behavior on color        { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }

        // 箭头角标
        Text {
            anchors { top: parent.top; right: parent.right; margins: 14 }
            text: "↗"
            font.pixelSize: 14; color: "#7c6fff"
            opacity: hov.containsMouse ? 1 : 0
            transform: Translate {
                x: hov.containsMouse ? 0 : -4
                y: hov.containsMouse ? 0 :  4
                Behavior on x { NumberAnimation { duration: 130 } }
                Behavior on y { NumberAnimation { duration: 130 } }
            }
            Behavior on opacity { NumberAnimation { duration: 130 } }
        }

        Column {
            id: cardInner
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
            spacing: 0

            Rectangle {
                width: 44; height: 44; radius: 12
                color: card.iconBg
                Text {
                    anchors.centerIn: parent
                    text: card.iconText
                    font.pixelSize: 20; color: "#ffffffcc"
                }
            }
            Item { height: 14 }
            Text {
                width: parent.width - 24
                text: card.cardName
                font.pixelSize: 14; font.weight: Font.DemiBold
                color: "#d0d0e8"; elide: Text.ElideRight
            }
            Item { height: 5 }
            Text {
                width: parent.width - 24
                text: card.cardDesc
                font.pixelSize: 12; color: "#50506a"
                lineHeight: 1.5; wrapMode: Text.WordWrap
            }
            Item { height: 12 }
            Rectangle {
                width: tagLabel.implicitWidth + 16; height: 20; radius: 6
                color: card.tagBg
                Text {
                    id: tagLabel
                    anchors.centerIn: parent
                    text: card.cardTag
                    font.pixelSize: 10; font.weight: Font.Bold
                    font.letterSpacing: 0.5; color: card.tagFg
                }
            }
        }

        MouseArea {
            id: hov
            anchors.fill: parent
            hoverEnabled: true
            cursorShape:  Qt.PointingHandCursor
            onClicked:    card.clicked(card.cardId)
        }
    }

    // ── 背景 ──────────────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: "#0f0f13"
    }

    // ── 主体：垂直滚动区 ──────────────────────────────────────────────────────
    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: mainCol.implicitHeight + 60
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: mainCol
            width: flick.width
            spacing: 0

            // ── 问候区 ───────────────────────────────────────────────────────
            Item {
                Layout.fillWidth: true
                implicitHeight: greetCol.implicitHeight + 56

                Column {
                    id: greetCol
                    anchors {
                        left: parent.left; right: parent.right
                        top: parent.top; topMargin: 40
                        leftMargin:  Math.max(40, (parent.width - 960) / 2)
                        rightMargin: Math.max(40, (parent.width - 960) / 2)
                    }
                    spacing: 8
                    Text {
                        text: "应用面板"
                        font.pixelSize: 26; font.weight: Font.DemiBold
                        color: "#e8e8f8"; font.letterSpacing: 0.3
                    }
                    Text {
                        text: "选择一个应用开始使用"
                        font.pixelSize: 13; color: "#55556a"
                    }
                }
            }

            // ── 内置应用分区 ─────────────────────────────────────────────────
            Item {
                Layout.fillWidth: true
                implicitHeight: builtinLabel.implicitHeight + 14 + builtinGrid.implicitHeight + 32

                property real hPad: Math.max(40, (flick.width - 960) / 2)

                Text {
                    id: builtinLabel
                    anchors { top: parent.top; topMargin: 8; left: parent.left; leftMargin: parent.hPad }
                    text: "内置应用"
                    font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 2
                    color: "#38385a"; font.capitalization: Font.AllUppercase
                }

                Grid {
                    id: builtinGrid
                    anchors {
                        top: builtinLabel.bottom; topMargin: 14
                        left: parent.left; right: parent.right
                        leftMargin: parent.hPad; rightMargin: parent.hPad
                    }
                    property int minColWidth: 200
                    property int colSpacing:  14
                    columns: Math.max(1, Math.floor((width + colSpacing) / (minColWidth + colSpacing)))
                    property real colWidth: (width - (columns - 1) * colSpacing) / columns
                    spacing: 14

                    PluginCard {
                        width: builtinGrid.colWidth
                        cardId: "player"; iconText: "▶"; iconBg: "#221e50"
                        cardName: "视频播放器"; cardDesc: "播放本地及网络视频，管理播放列表"
                        cardTag: "内置"; tagBg: "#1e1e35"; tagFg: "#7c6fff"
                        onClicked: (id) => root.builtinCardClicked(id)
                    }
                    PluginCard {
                        width: builtinGrid.colWidth
                        cardId: "playlist"; iconText: "☰"; iconBg: "#0c3530"
                        cardName: "播放列表"; cardDesc: "组织、排序和编辑媒体文件播放队列"
                        cardTag: "内置"; tagBg: "#1e1e35"; tagFg: "#7c6fff"
                        onClicked: (id) => root.builtinCardClicked(id)
                    }
                }
            }

            // ── 插件应用分区 ─────────────────────────────────────────────────
            Item {
                id: pluginSection
                Layout.fillWidth: true
                implicitHeight: pluginLabel.implicitHeight + 14 + pluginGrid.implicitHeight + 32

                property real hPad: Math.max(40, (flick.width - 960) / 2)

                Text {
                    id: pluginLabel
                    anchors { top: parent.top; topMargin: 8; left: parent.left; leftMargin: parent.hPad }
                    text: "已安装插件"
                    font.pixelSize: 11; font.weight: Font.Bold; font.letterSpacing: 2
                    color: "#38385a"; font.capitalization: Font.AllUppercase
                }

                Grid {
                    id: pluginGrid
                    anchors {
                        top: pluginLabel.bottom; topMargin: 14
                        left: parent.left; right: parent.right
                        leftMargin: parent.hPad; rightMargin: parent.hPad
                    }
                    property int minColWidth: 200
                    property int colSpacing:  14
                    columns: Math.max(1, Math.floor((width + colSpacing) / (minColWidth + colSpacing)))
                    property real colWidth: (width - (columns - 1) * colSpacing) / columns
                    spacing: 14

                    // 动态插件卡片
                    Repeater {
                        id: pluginRepeater
                        model: AppController.pluginsReady ? PluginManager.pluginCount : 0

                        PluginCard {
                            readonly property var iconList:  ["⬡", "◈", "⬢", "◇", "⊕", "◎"]
                            readonly property var colorList: ["#152a10", "#0d2040", "#2e1414",
                                                              "#2a1a00", "#1a1040", "#0c3530"]
                            width:    pluginGrid.colWidth
                            cardId:   "plugin:" + index
                            iconText: iconList[index % iconList.length]
                            iconBg:   colorList[index % colorList.length]
                            cardName: PluginManager.pluginName(index)
                            cardDesc: PluginManager.pluginDescriptionAt(index)
                            cardTag:  "插件"; tagBg: "#0d3020"; tagFg: "#2ecc71"
                            onClicked: (id) => root.pluginCardClicked(id)
                        }
                    }

                    // "安装更多插件" 占位卡
                    Rectangle {
                        width:  pluginGrid.colWidth
                        height: 140
                        color: "transparent"
                        border.color: "#ffffff0c"
                        border.width: 1
                        radius: 14

                        Column {
                            anchors.centerIn: parent
                            spacing: 8
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "+"; font.pixelSize: 26; color: "#28283a"
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "安装更多插件"; font.pixelSize: 12; color: "#32324a"
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: AppController.logInfo("HomePanel: open plugin store")
                        }
                    }
                }
            }

            Item { implicitHeight: 40 }
        }
    }

    // ── 插件加载遮罩 ──────────────────────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: "#0f0f13"
        opacity: AppController.pluginsReady ? 0 : 0.7
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 400 } }

        Column {
            anchors.centerIn: parent
            spacing: 14
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: true }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "正在加载插件…"; color: "#50507a"; font.pixelSize: 13
            }
        }
    }
}
