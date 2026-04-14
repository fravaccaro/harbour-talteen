import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    id: transferDialog

    property string incomingFileName: ""
    property string incomingFileSize: ""
    property bool hasSdCard: false
    property bool saveToSdCard: false

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        DialogHeader {
            acceptText: qsTr("Accept")
            cancelText: qsTr("Cancel")
        }

        Label {
            text: qsTr("Incoming backup")
            font.bold: true
            color: Theme.highlightColor
            anchors.horizontalCenter: parent.horizontalCenter
        }

        Label {
            text: incomingFileName + "\n(" + incomingFileSize + ")"
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeMedium
            color: Theme.primaryColor
        }

        TextSwitch {
            text: qsTr("Save to SD Card")
            description: qsTr("Save this backup on the external memory card")
            visible: hasSdCard // Only show if an SD card exists!
            checked: saveToSdCard
            onCheckedChanged: saveToSdCard = checked
        }

    }

}
