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
            width: parent.width - (Theme.paddingLarge * 2)
            text: qsTr("Receive backup?")
            font.pixelSize: Theme.fontSizeHuge
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            color: Theme.highlightColor
        }

        Label {
            width: parent.width - (Theme.paddingLarge * 2)
            text: incomingFileName + "\n(" + incomingFileSize + ")"
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            truncationMode: TruncationMode.Fade
            anchors.horizontalCenter: parent.horizontalCenter
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeExtraLarge
            color: Theme.secondaryHighlightColor
        }

        TextSwitch {
            text: qsTr("Save to SD card")
            description: qsTr("Save this backup to the SD card")
            visible: hasSdCard // Only show if an SD card exists!
            checked: saveToSdCard
            onCheckedChanged: saveToSdCard = checked
        }

    }

}
