import "../components"
import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    id: disclaimerDialog

    canAccept: acceptSwitch.checked

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingLarge

            DialogHeader {
                acceptText: qsTr("Continue")
                cancelText: qsTr("Exit")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeLarge
                text: qsTr("Disclaimer")
            }

            LabelText {
                text: qsTr("This software is provided \"as is\" without warranty of any kind. By using this app, you agree that the developers are not responsible for any data loss, file corruption, or device damage. Always keep multiple copies of your important backups.")
            }

            LabelSpacer {
            }

            TextSwitch {
                id: acceptSwitch

                text: qsTr("I have read and accept the risks")
            }

        }

    }

}
