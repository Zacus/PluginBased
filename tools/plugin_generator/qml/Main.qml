import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts
import PluginGenerator 1.0

ApplicationWindow {
    id: root

    width: 720
    height: 620
    minimumWidth: 640
    minimumHeight: 560
    visible: true
    title: "Plugin Generator"
    color: "#101116"

    PluginTemplateGenerator {
        id: generator
    }

    property bool withQml: true
    property string statusText: ""
    property bool statusOk: false
    property string iconPath: ""

    Component.onCompleted: outputField.text = generator.defaultOutputDir()

    FolderDialog {
        id: outputDialog
        title: "选择输出目录"
        onAccepted: outputField.text = selectedFolder.toString().replace("file://", "")
    }

    FileDialog {
        id: iconDialog
        title: "选择图片"
        nameFilters: ["Images (*.png *.jpg *.jpeg *.svg *.webp)", "All files (*)"]
        onAccepted: {
            root.iconPath = selectedFile.toString().replace("file://", "")
            iconPathField.text = root.iconPath
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#101116"
    }

    ColumnLayout {
        anchors {
            fill: parent
            margins: 28
        }
        spacing: 18

        Text {
            text: "插件结构生成器"
            color: "#f0f0f6"
            font.pixelSize: 24
            font.weight: Font.DemiBold
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 14
            rowSpacing: 12

            Text { text: "插件名"; color: "#a8a8b8"; font.pixelSize: 13 }
            TextField {
                id: pluginNameField
                Layout.fillWidth: true
                placeholderText: "MyPlugin"
                onTextEdited: {
                    if (displayNameField.text.length === 0)
                        displayNameField.text = text
                }
            }

            Text { text: "显示名"; color: "#a8a8b8"; font.pixelSize: 13 }
            TextField {
                id: displayNameField
                Layout.fillWidth: true
                placeholderText: pluginNameField.text.length > 0 ? pluginNameField.text : "My Plugin"
            }

            Text { text: "描述"; color: "#a8a8b8"; font.pixelSize: 13 }
            TextArea {
                id: descriptionField
                Layout.fillWidth: true
                implicitHeight: 72
                placeholderText: "Generated PluginBased plugin"
                wrapMode: TextEdit.WordWrap
            }

            Text { text: "文字图标"; color: "#a8a8b8"; font.pixelSize: 13 }
            TextField {
                id: iconField
                Layout.fillWidth: true
                text: "⬡"
            }

            Text { text: "图片图标"; color: "#a8a8b8"; font.pixelSize: 13 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    width: 42
                    height: 42
                    radius: 8
                    color: "#1b1d26"
                    border.color: "#ffffff18"
                    clip: true

                    Image {
                        anchors.fill: parent
                        anchors.margins: 7
                        source: root.iconPath.length > 0 ? ("file://" + root.iconPath) : ""
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        visible: root.iconPath.length > 0
                    }

                    Text {
                        anchors.centerIn: parent
                        text: iconField.text
                        color: "#ffffffcc"
                        font.pixelSize: 18
                        visible: root.iconPath.length === 0
                    }
                }

                TextField {
                    id: iconPathField
                    Layout.fillWidth: true
                    placeholderText: "可选：选择图片文件"
                    readOnly: true
                }

                Button {
                    text: "选择图片"
                    onClicked: iconDialog.open()
                }

                Button {
                    text: "清除"
                    enabled: root.iconPath.length > 0
                    onClicked: {
                        root.iconPath = ""
                        iconPathField.text = ""
                    }
                }
            }

            Text { text: "输出目录"; color: "#a8a8b8"; font.pixelSize: 13 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TextField {
                    id: outputField
                    Layout.fillWidth: true
                }

                Button {
                    text: "浏览"
                    onClicked: outputDialog.open()
                }
            }

            Text { text: "插件类型"; color: "#a8a8b8"; font.pixelSize: 13 }
            RowLayout {
                Layout.fillWidth: true
                spacing: 18

                RadioButton {
                    text: "带 QML 页面"
                    checked: root.withQml
                    onClicked: root.withQml = true
                }

                RadioButton {
                    text: "No-QML 后台插件"
                    checked: !root.withQml
                    onClicked: root.withQml = false
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Text {
                Layout.fillWidth: true
                text: root.statusText
                color: root.statusOk ? "#52d273" : "#ff6b6b"
                font.pixelSize: 13
                elide: Text.ElideRight
            }

            Button {
                text: "生成插件"
                onClicked: {
                    var result = generator.generate({
                        "pluginName": pluginNameField.text,
                        "displayName": displayNameField.text,
                        "description": descriptionField.text,
                        "icon": iconField.text,
                        "iconPath": root.iconPath,
                        "outputDir": outputField.text,
                        "withQml": root.withQml
                    })
                    root.statusOk = result.ok
                    root.statusText = result.ok ? (result.message + ": " + result.path) : result.message
                }
            }
        }
    }
}
