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

    label: qsTr("Save to")
    description: !isSdCardAvailable ? qsTr("SD card not detected") : qsTr("Available space: %1").arg(formattedFreeSpace)
    enabled: isSdCardAvailable && !isAppBusy
    opacity: !isSdCardAvailable ? 0.3 : (isAppBusy ? 0.3 : 1)
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
