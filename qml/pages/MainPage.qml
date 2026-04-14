import "../components/Utils.js" as SharedUtils
import QtQuick 2.0
import Sailfish.Silica 1.0
import org.harbour.talteen 1.0

Page {
    id: mainPage

    property bool isLoading: false

    // Function to ask C++ for the files and populate the list
    function loadBackups() {
        if (isLoading)
            return ;

        // Prevent double-loading
        isLoading = true;
        loaderTimer.start(); // Start the 50ms countdown
    }

    allowedOrientations: Orientation.All
    // Load files when app starts, and refresh when returning to this page
    Component.onCompleted: loadBackups()
    onStatusChanged: {
        if (status === PageStatus.Active)
            loadBackups();

    }

    // Gives the UI 50ms to draw the BusyIndicator before C++ freezes the thread
    Timer {
        id: loaderTimer

        interval: 50
        onTriggered: {
            backupsModel.clear();
            var files = appCore.getBackupFiles();
            for (var i = 0; i < files.length; i++) {
                backupsModel.append(files[i]);
            }
            isLoading = false; // Hide the indicator when done
        }
    }

    Talteen {
        id: appCore
    }

    // This model holds the list of backups
    ListModel {
        id: backupsModel
    }

    SilicaListView {
        anchors.fill: parent
        model: backupsModel

        // Menu
        PullDownMenu {
            MenuItem {
                text: qsTr("About")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }

            MenuItem {
                text: qsTr("Receive from another device")
                onClicked: pageStack.push(Qt.resolvedUrl("ReceivePage.qml"))
            }

            MenuItem {
                text: qsTr("New backup")
                // Pushes the Backup page to the stack
                onClicked: pageStack.push(Qt.resolvedUrl("BackupPage.qml"))
            }

        }

        // BUSY INDICATOR
        BusyIndicator {
            size: BusyIndicatorSize.Large
            anchors.centerIn: parent
            running: isLoading
            visible: isLoading
        }

        // Shows a placeholder if the folder is completely empty
        ViewPlaceholder {
            // Do not show "No backups found" while loading
            enabled: backupsModel.count === 0 && !isLoading
            text: qsTr("No backups found")
            hintText: qsTr("Pull down to save a new backup or receive one from a nearby device")
        }

        header: PageHeader {
            title: "Talteen"
        }

        // LIST ITEMS
        delegate: ListItem {
            id: listItem

            contentHeight: Theme.itemSizeMedium
            onClicked: {
                pageStack.push(Qt.resolvedUrl("RestorePage.qml"), {
                    "selectedBackupPath": model.path,
                    "backupName": model.name,
                    "backupLabel": model.label,
                    "backupDate": model.date,
                    "backupSize": SharedUtils.formatBytes(model.size)
                });
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                x: Theme.horizontalPageMargin
                width: parent.width - (Theme.horizontalPageMargin * 2)

                Label {
                    text: model.label
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    color: listItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                }

                // Subtitle
                Label {
                    text: model.date + " • " + SharedUtils.formatBytes(model.size) + " • " + model.location
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }

            }

            // LONG PRESS MENU
            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Send to a nearby device")
                    onClicked: pageStack.push(Qt.resolvedUrl("SendPage.qml"), {
                        "selectedFile": model.path,
                        "backupName": model.name,
                        "backupLabel": model.label,
                        "backupDate": model.date,
                        "backupSize": SharedUtils.formatBytes(model.size)
                    })
                }

                MenuItem {
                    text: qsTr("Delete")
                    onClicked: {
                        listItem.remorseAction(qsTr("Deleting"), function() {
                            if (appCore.deleteBackup(model.path))
                                backupsModel.remove(index);

                        });
                    }
                }

            }

        }

    }

}
