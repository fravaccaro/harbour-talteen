// components/StorageDestinationButton.qml
import QtQuick 2.0
import Sailfish.Silica 1.0
import "Utils.js" as SharedUtils

ComboBox {
    id: storageBtn

    property var backendEngine
    property bool isSdCardAvailable: false
    property bool saveToSdCard: false
    property bool isAppBusy: false
    property string formattedFreeSpace: ""

    function updateFreeSpace() {
        if (backendEngine) {
            // Check based on current selection
            var bytes = backendEngine.getFreeSpace(currentIndex === 1);
            formattedFreeSpace = SharedUtils.formatBytes(bytes);
        }
    }

    label: qsTr("Destination")
    description: !isSdCardAvailable ? qsTr("SD card not detected") : qsTr("Available space:") + " " + formattedFreeSpace
    enabled: isSdCardAvailable && !isAppBusy
    opacity: !isSdCardAvailable ? 0.2 : (isAppBusy ? 0.3 : 1)
    // Keep state in sync with external saveToSdCard property
    currentIndex: saveToSdCard ? 1 : 0
    onCurrentIndexChanged: {
        updateFreeSpace();
    }
    Component.onCompleted: updateFreeSpace()

    menu: ContextMenu {
        MenuItem {
            text: qsTr("Internal storage")
            onClicked: saveToSdCard = false
        }

        MenuItem {
            text: qsTr("SD card")
            onClicked: saveToSdCard = true
        }

    }

}
