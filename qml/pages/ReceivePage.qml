import "../components"
import "../components/Utils.js" as SharedUtils
import Nemo.KeepAlive 1.2
import QtQuick 2.0
import Sailfish.Silica 1.0
import org.harbour.talteen 1.0

Page {
    id: receivePage

    property bool isTransferRunning: false
    property string statusMessage
    property real currentProgress: 0
    // Property to remember the incoming file name
    property string incomingFileName: ""

    allowedOrientations: Orientation.All
    backNavigation: !isTransferRunning
    onIsTransferRunningChanged: {
        appWindow.isAppWorking = isTransferRunning;
        appWindow.appWorkingText = qsTr("Receiving file...");
    }
    Component.onCompleted: netTransfer.startReceiving(45455)

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
            appWindow.showToast(status);
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
                    isTransferRunning = true;
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
        onTransferAborted: {
            if (pageStack.currentPage.objectName === "TransferPromptDialog")
                pageStack.pop();

        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            enabled: !isTransferRunning
            busy: netTransfer.isListening || isTransferRunning

            MenuItem {
                // Show this option ONLY when idle
                visible: !isTransferRunning
                text: netTransfer.isListening ? qsTr("Stop receiving") : qsTr("Start receiving")
                onClicked: {
                    if (netTransfer.isListening)
                        netTransfer.stopReceiving();
                    else
                        netTransfer.startReceiving(45455);
                }
            }

        }

        ViewPlaceholder {
            // Only show the placeholder when no file is currently transferring
            enabled: !isTransferRunning
            // Dynamically change the text based on whether the server is on or off
            text: netTransfer.isListening ? qsTr("Ready to receive") : qsTr("Receive mode off")
            hintText: netTransfer.isListening ? qsTr("Pull down to stop receiving") : qsTr("Pull down to start receiving")
        }

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Receive")
            }

            StatusLabel {
                text: statusMessage
                visible: isTransferRunning
            }

            ProgressStatusBar {
                minimumValue: 0
                maximumValue: 1
                value: currentProgress
                valueText: Math.round(value * 100) + "%"
                enabled: isTransferRunning
                opacity: isTransferRunning ? 1 : 0
            }

            ActionButton {
                text: qsTr("Stop transfer")
                visible: isTransferRunning
                onClicked: netTransfer.cancelTransfer()
            }

            LabelSpacer {
            }

        }

    }

    KeepAlive {
        enabled: isTransferRunning
    }

}
