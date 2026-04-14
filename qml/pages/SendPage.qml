import "../components"
import Nemo.KeepAlive 1.2
import QtQuick 2.0
import Sailfish.Silica 1.0
import org.harbour.talteen 1.0

Page {
    id: sendPage

    property string selectedFile: ""
    property string backupName: ""
    property string backupLabel: ""
    property string backupDate: ""
    property string backupSize: ""
    property bool isTransferRunning: false
    property string statusMessage: qsTr("Searching for nearby devices..")
    property real currentProgress: 0

    allowedOrientations: Orientation.All
    backNavigation: !isTransferRunning
    onIsTransferRunningChanged: {
        appWindow.isAppWorking = isTransferRunning;
        appWindow.appWorkingText = qsTr("Sending backup...");
    }
    // Auto-start discovering as soon as the page opens
    Component.onCompleted: netTransfer.discoverDevices()

    ListModel {
        id: devicesModel
    }

    NetworkTransfer {
        id: netTransfer

        onProgressChanged: {
            currentProgress = progress;
            // UPDATE THE LIVE NOTIFICATION
            if (progress > 0 && progress < 1) {
                appWindow.showProgressNotification(qsTr("Transferring"), selectedFile.split('/').pop(), progress);
            } else if (progress === 1) {
                isTransferRunning = false;
                appWindow.showNotification(qsTr("Transfer Complete"), qsTr("File sent successfully!"));
            } else if (progress === 0) {
                isTransferRunning = false;
            }
        }
        onStatusChanged: {
            statusMessage = status;
        }
        onDeviceDiscovered: {
            var found = false;
            for (var i = 0; i < devicesModel.count; i++) {
                if (devicesModel.get(i).ipAddress === ipAddress) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                devicesModel.append({
                    "ipAddress": ipAddress
                });
                statusMessage = qsTr("Devices found:") + " " + devicesModel.count;
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            enabled: !isTransferRunning

            MenuItem {
                text: netTransfer.isDiscovering ? qsTr("Stop search") : qsTr("Search for nearby devices")
                onClicked: {
                    if (netTransfer.isDiscovering) {
                        netTransfer.stopDiscovery();
                    } else {
                        devicesModel.clear();
                        netTransfer.discoverDevices();
                    }
                }
            }

        }

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Send Backup")
            }

            StatusLabel {
                text: statusMessage
            }

            ProgressBar {
                width: parent.width - (Theme.paddingLarge * 2)
                anchors.horizontalCenter: parent.horizontalCenter
                minimumValue: 0
                maximumValue: 1
                value: currentProgress
                valueText: Math.round(value * 100) + "%"
                visible: isTransferRunning
                opacity: isTransferRunning ? 1 : 0

                Behavior on opacity {
                    FadeAnimation {
                    }

                }

            }

            LabelSpacer {
            }

            SectionHeader {
                text: qsTr("Details")
            }

            DetailItem {
                label: qsTr("File")
                // Fallback to splitting the path if backupName is empty
                value: backupName !== "" ? backupName : selectedFile.split('/').pop()
            }

            DetailItem {
                label: qsTr("Label")
                value: backupLabel
                visible: backupLabel !== "" // Hide if missing
            }

            DetailItem {
                label: qsTr("Date")
                value: backupDate
                visible: backupDate !== "" // Hide if missing
            }

            DetailItem {
                label: qsTr("Size")
                value: backupSize
                visible: backupSize !== "" // Hide if missing
            }

            LabelSpacer {
            }

            ComboBox {
                id: deviceSelector

                width: parent.width
                label: qsTr("Send to")
                visible: devicesModel.count > 0 && !isTransferRunning

                menu: ContextMenu {
                    Repeater {
                        model: devicesModel

                        MenuItem {
                            text: ipAddress
                        }

                    }

                }

            }

            Button {
                text: isTransferRunning ? qsTr("Cancel Transfer") : qsTr("Send")
                anchors.horizontalCenter: parent.horizontalCenter
                enabled: (devicesModel.count > 0 && !isTransferRunning) || isTransferRunning
                opacity: enabled ? 1 : 0.3
                onClicked: {
                    if (isTransferRunning) {
                        netTransfer.cancelTransfer();
                        isTransferRunning = false;
                        statusMessage = qsTr("Transfer cancelled");
                    } else {
                        var selectedIp = devicesModel.get(deviceSelector.currentIndex).ipAddress;
                        if (netTransfer.isDiscovering)
                            netTransfer.stopDiscovery();

                        isTransferRunning = true;
                        netTransfer.sendFile(selectedIp, 45455, selectedFile);
                    }
                }
            }

        }

    }

    KeepAlive {
        enabled: isTransferRunning
    }

}
