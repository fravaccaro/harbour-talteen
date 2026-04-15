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
    property string statusMessage
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
                appWindow.showProgressNotification(qsTr("Sending"), selectedFile.split('/').pop(), progress);
            } else if (progress === 1) {
                isTransferRunning = false;
                appWindow.showNotification(qsTr("Sending complete"), qsTr("Backup sent successfully"));
            } else if (progress === 0) {
                isTransferRunning = false;
            }
        }
        onStatusChanged: {
            statusMessage = status;
        }
        onIsDiscoveringChanged: {
            if (isDiscovering)
                appWindow.showToast(qsTr("Looking for nearby devices..."));
            else
                appWindow.showToast(qsTr("Search off"));
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
                var msg = qsTr("%n device(s) found", "", devicesModel.count);
                statusMessage = msg;
                appWindow.showToast(msg);
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            enabled: !isTransferRunning
            busy: netTransfer.isDiscovering || isTransferRunning

            MenuItem {
                text: netTransfer.isDiscovering ? qsTr("Stop looking") : qsTr("Look for nearby devices")
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
                title: qsTr("Send backup")
            }

            StatusLabel {
                text: statusMessage
            }

            ProgressStatusBar {
                minimumValue: 0
                maximumValue: 1
                value: currentProgress
                valueText: Math.round(value * 100) + "%"
                enabled: isTransferRunning
                opacity: isTransferRunning ? 1 : 0
            }

            LabelSpacer {
            }

            SectionHeader {
                text: qsTr("Details")
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

            ActionButton {
                text: isTransferRunning ? qsTr("Stop sending") : qsTr("Send")
                enabled: (devicesModel.count > 0 && !isTransferRunning) || isTransferRunning
                onClicked: {
                    if (isTransferRunning) {
                        netTransfer.cancelTransfer();
                        isTransferRunning = false;
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
