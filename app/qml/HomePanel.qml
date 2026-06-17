import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PluginBased 1.0
import QuickUI.Components 1.0

Item {
    id: root

    signal pluginCardClicked(string pluginId, int pluginIndex)

    // ── 内联组件：必须在根对象内部定义 ──────────────────────────────────────

    component PluginCard: Rectangle {
        id: card

        property string cardId:   ""
        property string iconText: "▶"
        property url iconUrl:     ""
        property color iconBg:    ComponentTheme.accent
        property string cardName: ""
        property string cardDesc: ""
        property string cardTag:  ""
        property color tagBg:     ComponentTheme.surfaceHover
        property color tagFg:     ComponentTheme.accent

        signal clicked(string id)

        implicitHeight: cardInner.implicitHeight + 44
        height: implicitHeight

        color: hov.containsMouse ? ComponentTheme.surfaceHover : ComponentTheme.surface
        border.color: hov.containsMouse ? ComponentTheme.accent : ComponentTheme.separator
        border.width: 1
        radius: ComponentTheme.buttonRadius

        Behavior on color        { ColorAnimation { duration: ComponentTheme.durationFast } }
        Behavior on border.color { ColorAnimation { duration: ComponentTheme.durationFast } }

        // 箭头角标
        Text {
            anchors { top: parent.top; right: parent.right; margins: 14 }
            text: "↗"
            font.pixelSize: 14; color: ComponentTheme.accent
            opacity: hov.containsMouse ? 1 : 0
            transform: Translate {
                x: hov.containsMouse ? 0 : -4
                y: hov.containsMouse ? 0 :  4
                Behavior on x { NumberAnimation { duration: ComponentTheme.durationFast } }
                Behavior on y { NumberAnimation { duration: ComponentTheme.durationFast } }
            }
            Behavior on opacity { NumberAnimation { duration: ComponentTheme.durationFast } }
        }

        Column {
            id: cardInner
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
            spacing: 0

            Rectangle {
                width: 44; height: 44; radius: 12
                color: card.iconBg
                clip: true
                Image {
                    anchors.fill: parent
                    anchors.margins: 8
                    source: card.iconUrl
                    visible: card.iconUrl.toString().length > 0
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                Text {
                    anchors.centerIn: parent
                    visible: card.iconUrl.toString().length === 0
                    text: card.iconText
                    font.pixelSize: 20
                    color: ComponentTheme.textOnAccent
                }
            }
            Item { height: 14 }
            Text {
                width: parent.width - 24
                text: card.cardName
                font.pixelSize: 14; font.weight: Font.DemiBold
                color: ComponentTheme.textPrimary
                elide: Text.ElideRight
            }
            Item { height: 5 }
            Text {
                width: parent.width - 24
                text: card.cardDesc
                font.pixelSize: 12
                color: ComponentTheme.textSecondary
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
                    font.letterSpacing: 0
                    color: card.tagFg
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
        color: ComponentTheme.surface
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
                        text: AppController.currentLanguage, qsTr("Application Panel")
                        font.pixelSize: 26; font.weight: Font.DemiBold
                        color: ComponentTheme.textPrimary
                        font.letterSpacing: 0
                    }
                    Text {
                        text: AppController.currentLanguage, qsTr("Select an application to start")
                        font.pixelSize: 13
                        color: ComponentTheme.textSecondary
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
                    text: AppController.currentLanguage, qsTr("Installed Plugins")
                    font.pixelSize: 11
                    font.weight: Font.Bold
                    font.letterSpacing: 0
                    color: ComponentTheme.textDisabled
                    font.capitalization: Font.AllUppercase
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
                            readonly property var iconBgList: ["#18352f", "#1b323d", "#3a2e22",
                                                               "#26363a", "#29333a", "#163a36"]

                            width:    pluginGrid.colWidth
                            cardId:   "plugin:" + index
                            iconText: PluginManager.pluginCardIcon(index)
                            iconUrl:  PluginManager.pluginCardIconUrl(index)
                            iconBg:   iconBgList[index % iconBgList.length]
                            cardName: AppController.currentLanguage, PluginManager.pluginCardName(index)
                            cardDesc: AppController.currentLanguage, PluginManager.pluginDescriptionAt(index)
                            cardTag:  AppController.currentLanguage, qsTr("Plugin")
                            tagBg: ComponentTheme.surfaceHover
                            tagFg: ComponentTheme.accent
                            onClicked: (id) => root.pluginCardClicked(id, index)
                        }
                    }

                    // "安装更多插件" 占位卡
                    Rectangle {
                        width:  pluginGrid.colWidth
                        height: 140
                        color: "transparent"
                        border.color: ComponentTheme.separator
                        border.width: 1
                        radius: ComponentTheme.buttonRadius

                        Column {
                            anchors.centerIn: parent
                            spacing: 8
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "+"
                                font.pixelSize: 26
                                color: ComponentTheme.textDisabled
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: AppController.currentLanguage, qsTr("Install More Plugins")
                                font.pixelSize: 12
                                color: ComponentTheme.textSecondary
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
        color: ComponentTheme.surface
        opacity: AppController.pluginsReady ? 0 : 0.7
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: ComponentTheme.durationNormal } }

        Column {
            anchors.centerIn: parent
            spacing: 14
            BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: true }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: AppController.currentLanguage, qsTr("Loading plugins...")
                color: ComponentTheme.textSecondary
                font.pixelSize: 13
            }
        }
    }
}
