import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PluginBased 1.0
import QuickUI.Components 1.0

Dialog {
    id: dialog
    modal: true
    title: AppController.currentLanguage, qsTr("About")
    anchors.centerIn: Overlay.overlay
    closePolicy: Popup.CloseOnEscape
    width: 368
    padding: 24

    background: Rectangle {
        color: ComponentTheme.surface
        border.color: ComponentTheme.separator
        radius: 8
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: AppController.appName
            color: ComponentTheme.textPrimary
            font.pixelSize: 22
            font.bold: true
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: AppController.currentLanguage,
                  qsTr("Version %1").arg(AppController.appVersion)
            color: ComponentTheme.textSecondary
            font.pixelSize: 14
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: AppController.currentLanguage,
                  qsTr("Build %1").arg(AppController.buildNumber)
            color: ComponentTheme.textSecondary
            font.pixelSize: 14
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 8
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                text: AppController.currentLanguage, qsTr("Copy diagnostic information")
                onClicked: AppController.copyBuildDiagnosticInfo()
            }

            Button {
                text: AppController.currentLanguage, qsTr("Close")
                onClicked: dialog.close()
            }
        }
    }
}
