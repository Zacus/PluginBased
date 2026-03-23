import QtQuick
import QtQuick.Controls.Basic

// 进度/Seek 滑块：自定义外观，支持点击任意位置跳转
Slider {
    id: root

    from:    0.0
    to:      1.0
    value:   0.0

    // 覆盖默认 track 高度使其更易点击
    implicitHeight: 20

    background: Item {
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width:  parent.width
            height: 4
            radius: 2
            color: "#2a2a3a"

            // 已播放部分
            Rectangle {
                width:  root.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color:  root.enabled ? "#7c6fff" : "#44445a"

                Behavior on width { NumberAnimation { duration: 100 } }
            }

            // 鼠标悬停时显示缓冲区（演示，固定30%）
            Rectangle {
                visible: trackMouse.containsMouse
                x: 0
                width:  0.30 * parent.width
                height: parent.height
                radius: parent.radius
                color:  "#ffffff12"
                z: -1
            }
        }

        // 扩大鼠标命中区
        MouseArea {
            id: trackMouse
            anchors { fill: parent; topMargin: -8; bottomMargin: -8 }
            hoverEnabled: true
            propagateComposedEvents: true
        }
    }

    handle: Rectangle {
        x: root.leftPadding + root.visualPosition *
           (root.availableWidth - width)
        y: root.topPadding  + root.availableHeight / 2 - height / 2
        width:  14; height: 14; radius: 7
        color:  root.pressed ? "#9d90ff" : "#7c6fff"
        border.color: "#ffffff30"
        visible: root.enabled

        Behavior on color { ColorAnimation { duration: 80 } }

        // 悬停时放大
        scale: root.hovered ? 1.25 : 1.0
        Behavior on scale { NumberAnimation { duration: 120 } }
    }
}
