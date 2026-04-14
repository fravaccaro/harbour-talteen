import "../components"
import Nemo.Notifications 1.0
import QtQuick 2.0
import Sailfish.Silica 1.0
import org.harbour.talteen 1.0

Page {
    id: backupPage

    property bool isBackupRunning: false
    property bool allSelected: false
    property bool noneSelected: true

    // Helper functions for the pulley menu
    function setAllSwitches(state) {
        for (var i = 0; i < categoriesModel.count; i++) {
            categoriesModel.setProperty(i, "isChecked", state);
        }
        updateSelectionStates();
    }

    function updateSelectionStates() {
        var all = true;
        var none = true;
        for (var i = 0; i < categoriesModel.count; i++) {
            if (categoriesModel.get(i).isChecked)
                none = false;
            else
                all = false;
        }
        allSelected = all;
        noneSelected = none;
    }

    allowedOrientations: Orientation.All
    // Prevent swipe to go back while backup is ongoing
    backNavigation: !isBackupRunning
    onIsBackupRunningChanged: {
        appWindow.isAppWorking = isBackupRunning;
        appWindow.appWorkingText = qsTr("Saving backup...");
    }

    Talteen {
        id: appCore

        onBackupFinished: {
            isBackupRunning = false;
            console.log(message);
            appWindow.showNotification(success ? qsTr("Backup completed") : qsTr("Backup failed"), message);
        }
    }

    // Categories model
    ListModel {
        id: categoriesModel

        Component.onCompleted: {
            append({
                "key": "appdata",
                "name": qsTr("App data"),
                "section": qsTr("Applications"),
                "isChecked": false
            });
            append({
                "key": "apporder",
                "name": qsTr("App order"),
                "section": "",
                "isChecked": false
            });
            append({
                "key": "calls",
                "name": qsTr("Call history"),
                "section": qsTr("Communication"),
                "isChecked": false
            });
            append({
                "key": "messages",
                "name": qsTr("Messages"),
                "section": "",
                "isChecked": false
            });
            append({
                "key": "documents",
                "name": qsTr("Documents"),
                "section": qsTr("Files"),
                "isChecked": false
            });
            append({
                "key": "downloads",
                "name": qsTr("Downloads"),
                "section": "",
                "isChecked": false
            });
            append({
                "key": "pictures",
                "name": qsTr("Pictures"),
                "section": qsTr("Media"),
                "isChecked": false
            });
            append({
                "key": "videos",
                "name": qsTr("Videos"),
                "section": "",
                "isChecked": false
            });
            append({
                "key": "music",
                "name": qsTr("Music"),
                "section": "",
                "isChecked": false
            });
        }
    }

    SilicaFlickable {
        id: backupView

        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge
        // Disable while backup is ongoing
        enabled: !isBackupRunning
        opacity: isBackupRunning ? 0.2 : 1

        PullDownMenu {
            MenuItem {
                text: qsTr("Unselect all")
                enabled: !noneSelected
                onClicked: setAllSwitches(false)
            }

            MenuItem {
                text: qsTr("Select all")
                enabled: !allSelected
                onClicked: setAllSwitches(true)
            }

        }

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("New backup")
            }

            TextField {
                id: backupLabelField

                focus: false
                width: parent.width
                //placeholderText: qsTr("e.g. , Before system update")
                label: qsTr("Label (optional)")
                // Closes the keyboard when the user presses Enter
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false

                validator: RegExpValidator {
                    regExp: /^[a-zA-Z0-9+\-_]{3,}$/
                }

            }

            StorageDestinationButton {
                id: storageBtn

                width: parent.width
                backendEngine: appCore
                isSdCardAvailable: appCore.getSdCardPath() !== ""
                isAppBusy: isBackupRunning
            }

            Repeater {
                model: categoriesModel

                delegate: Column {
                    width: parent.width

                    SectionHeader {
                        text: model.section
                        visible: model.section !== ""
                    }

                    TextSwitch {
                        text: model.name
                        checked: model.isChecked
                        onCheckedChanged: {
                            if (model.isChecked !== checked) {
                                categoriesModel.setProperty(index, "isChecked", checked);
                                updateSelectionStates();
                            }
                        }
                    }

                }

            }

            LabelSpacer {
            }

            Button {
                text: qsTr("Start backup")
                anchors.horizontalCenter: parent.horizontalCenter
                enabled: !noneSelected
                onClicked: {
                    isBackupRunning = true;
                    var backupOptions = {
                        "destination": storageBtn.saveToSdCard ? appCore.getSdCardPath() : "internal",
                        "label": backupLabelField.text
                    };
                    for (var i = 0; i < categoriesModel.count; i++) {
                        var item = categoriesModel.get(i);
                        backupOptions[item.key] = item.isChecked;
                    }
                    appCore.startBackup(backupOptions);
                    appWindow.showProgressNotification(qsTr("Backup"), qsTr("Saving backup..."), Notification.ProgressIndeterminate);
                }
            }

            LabelSpacer {
            }

        }

        Behavior on opacity {
            FadeAnimation {
            }

        }

    }

    // PROGRESS BAR OVERLAY
    Column {
        anchors.centerIn: parent
        width: parent.width - (Theme.paddingLarge * 2)
        visible: isBackupRunning
        spacing: Theme.paddingMedium

        ProgressBar {
            width: parent.width
            indeterminate: true
            label: qsTr("Saving backup...")
        }

    }

}
