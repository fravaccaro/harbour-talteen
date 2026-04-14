// components/StorageDestinationButton.qml
import QtQuick 2.0
import Sailfish.Silica 1.0
import "Utils.js" as SharedUtils

ValueButton {
    id: storageBtn

    // Properties passed from the parent page
    property var backendEngine
    property bool isSdCardAvailable: false
    property bool saveToSdCard: false
    property bool isAppBusy: false
    // Internal property for UI
    property string formattedFreeSpace: ""

    function updateFreeSpace() {
        if (backendEngine) {
            var bytes = backendEngine.getFreeSpace(saveToSdCard);
            formattedFreeSpace = SharedUtils.formatBytes(bytes);
        }
    }

    label: qsTr("Destination")
    value: saveToSdCard ? qsTr("SD card") : qsTr("Internal storage")
    description: !isSdCardAvailable ? qsTr("SD card not detected") : qsTr("Available space:") + " " + formattedFreeSpace
    enabled: isSdCardAvailable && !isAppBusy
    opacity: enabled ? 1 : 0.3
    onClicked: {
        saveToSdCard = !saveToSdCard;
        updateFreeSpace();
    }
    Component.onCompleted: updateFreeSpace()
}
