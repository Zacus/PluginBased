import QtQuick
import QtQuick.Controls.Basic

// 通用图标按钮：纯文本/Emoji 图标 + tooltip + hover 效果
Item {
    id: root

    property string iconText: "?"
    property string tooltip:  ""
    property int    fontSize: 16

    signal clicked()

    width:  34
    height: 34

    Rectangle {
        anchors.fill: parent
        radius: 6
        color:  mouseArea.pressed
                ? "#ffffff18"
                : mouseArea.containsMouse ? "#ffffff0e" : "transparent"

        Behavior on color { ColorAnimation { duration: 80 } }

        Text {
            anchors.centerIn: parent
            text:       root.iconText
            font.pixelSize: root.fontSize
            color: mouseArea.pressed ? "#ffffffcc" : "#ffffff99"
        }
    }

    // Tooltip
    ToolTip.visible:  mouseArea.containsMouse && root.tooltip.length > 0
    ToolTip.text:     root.tooltip
    ToolTip.delay:    600

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape:  Qt.PointingHandCursor
        onClicked:    root.clicked()
    }
}
