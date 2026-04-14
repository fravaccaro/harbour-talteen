import "../components"
import "../components/Utils.js" as SharedUtils
import QtQuick 2.0
import Sailfish.Silica 1.0
import org.harbour.talteen 1.0

Page {
    id: receivePage

    property bool isTransferRunning: false
    property string statusMessage: qsTr("Ready to receive")
    property real currentProgress: 0
    // Property to remember the incoming file name
    property string incomingFileName: ""

    allowedOrientations: Orientation.All
    backNavigation: !isTransferRunning
    onIsTransferRunningChanged: {
        appWindow.isAppWorking = isTransferRunning;
        appWindow.appWorkingText = qsTr("Receiving file...");
    }

    NetworkTransfer {
        id: netTransfer

        onProgressChanged: {
            currentProgress = progress;
            if (progress > 0 && progress < 1) {
                appWindow.showProgressNotification(qsTr("Receiving"), incomingFileName, progress);
            } else if (progress === 1) {
                isTransferRunning = false;
                appWindow.showNotification(qsTr("Backup received"), qsTr("Backup received from another device"));
            } else if (progress === 0) {
                isTransferRunning = false;
            }
        }
        onStatusChanged: {
            statusMessage = status;
        }
        onTransferRequested: {
            // Save the filename so the Progress indicator can use it
            incomingFileName = fileName;
            var dialog = pageStack.push(Qt.resolvedUrl("TransferPromptDialog.qml"), {
                "incomingFileName": fileName,
                "incomingFileSize": SharedUtils.formatBytes(fileSize),
                "hasSdCard": netTransfer.hasSdCard()
            });
            // Safety check so it doesn't crash if the file is missing
            if (dialog) {
                dialog.accepted.connect(function() {
                    var useSdCard = dialog.saveToSdCard;
                    netTransfer.acceptTransfer(useSdCard);
                });
                dialog.rejected.connect(function() {
                    netTransfer.rejectTransfer();
                });
            } else {
                console.log("ERROR: Could not load TransferPromptDialog.qml!");
                netTransfer.rejectTransfer(); // Auto-reject so the socket doesn't hang
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Receive")
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
                visible: currentProgress > 0 && currentProgress < 1
            }

            LabelSpacer {
            }

            Button {
                // --- FIX 4: Removed saveToSdCard from here because the C++ was updated! ---

                text: netTransfer.isListening ? (isTransferRunning ? qsTr("Stop Transfer") : qsTr("Hide Device")) : qsTr("Wait for Backup")
                anchors.horizontalCenter: parent.horizontalCenter
                enabled: !isTransferRunning || netTransfer.isListening
                opacity: enabled ? 1 : 0.3
                onClicked: {
                    if (netTransfer.isListening)
                        netTransfer.stopReceiving();
                    else
                        netTransfer.startReceiving(45455);
                }
            }

        }

    }

}
