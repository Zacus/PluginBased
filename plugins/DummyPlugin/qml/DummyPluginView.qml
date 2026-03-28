import QtQuick
import QtQuick.Layouts
import QuickUI.Components 1.0      // PlayerEngine、PlaylistModel、PlaybackContext 等

Item {
    id: root

    // 宿主读取此属性作为顶部工具栏副标题
    property string pageTitle: "组件测试"

    RowLayout {
        anchors.fill: parent
        spacing: 0

        TimelineModel {
            id: myTimelineModel

            Component.onCompleted: {
                // addSegment(startMs, endMs, type)
                // 用 Date 构造 ms 时间戳
                var base = new Date("2024-01-01T00:00:00Z").getTime()
                addSegment(base,                base + 3600000,  0)  // 普通录像 1h
                addSegment(base + 5400000,      base + 7200000,  1)  // 移动侦测
                addSegment(base + 9000000,      base + 10800000, 2)  // 报警
            }
        }


        TimelineView {
            id: timeline
            width:  parent.width
            height: 120
            model:  myTimelineModel          // C++ TimelineModel*

            // 播放器位置 → 游标（单向驱动）
            currentTime: myTimelineModel.totalStart

            // 用户 seek → 可在此接入 PlayerEngine；示例无播放器时仅占位
            onSeeked: function(t) {
                console.log("[DummyPlugin] seek(ms):", t)
            }
        }

        // 外部按钮
        Button {
            text: "全览"
            onClicked: timeline.fitAll()
        }
        Button {
            text: "放大"
            onClicked: timeline.zoom(1.5)
        }
    }
}
