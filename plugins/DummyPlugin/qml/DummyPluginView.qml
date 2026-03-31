import QtQuick
import QtQuick.Layouts
import QuickUI.Components 1.0      // PlayerEngine、PlaylistModel、PlaybackContext 等

Item {
    id: root

    property string pageTitle: "组件测试"

    // ── 非视觉对象放在 Item 层级 ─────────────────────────────
    TimelineModel {
        id: myTimelineModel

        Component.onCompleted: {
            var base = new Date("2024-01-01T00:00:00Z").getTime()
            addSegment(base,           base + 3600000,  0)  // 普通录像 1h
            addSegment(base + 5400000, base + 7200000,  1)  // 移动侦测
            addSegment(base + 9000000, base + 10800000, 2)  // 报警
        }
    }

    // 当前播放时间，定义在 root 上才能被所有子组件访问
    property real playPosition: 0  // 从一个非起始位置开始，测试自动调整功能

    // 数据加载完成后初始化播放位置
    Connections {
        target: myTimelineModel
        function onBoundsChanged() {
            console.log("totalStart:", myTimelineModel.totalStart, "totalEnd:", myTimelineModel.totalEnd )
            if (root.playPosition === 0 && myTimelineModel.totalStart > 0)
                //播放位置从有数据开始，而不是从0开始（如果0没有数据的话）
                root.playPosition = myTimelineModel.totalStart
        }
    }

    // 模拟播放器：每 100ms 推进一次（实时速度）
    Timer {
        id: playTimer
        interval: 100
        repeat: true
        running: true
        onTriggered: {
            root.playPosition += 100000
            if (root.playPosition > myTimelineModel.totalEnd)
                root.playPosition = myTimelineModel.totalStart
        }
    }

    // ── 布局 ─────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 时间轴
        TimelineView {
            id: timeline
            Layout.fillWidth: true
            height: 120
            model: myTimelineModel
            followMode:  "center"
            currentTime: root.playPosition

            onSeeked: function(t) {
                root.playPosition = t
                console.log("[DummyPlugin] seek(ms):", t)
            }
        }

        // 控制按钮行
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Layout.margins: 8

            Button {
                text: "全览"
                onClicked: timeline.fitAll()
            }
            Button {
                text: "放大"
                onClicked: timeline.zoom(1.5)
            }
            Button {
                text: playTimer.running ? "暂停" : "播放"
                onClicked: playTimer.running = !playTimer.running
            }
        }

        // 占位填充剩余空间
        Item { Layout.fillWidth: true; Layout.fillHeight: true }
    }
}
