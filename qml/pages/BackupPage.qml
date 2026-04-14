import "../components"
import Nemo.KeepAlive 1.2
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
        // Disable while backup is ongoing

        id: backupView

        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            enabled: !isBackupRunning

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

                enabled: !isBackupRunning
                focus: false
                width: parent.width
                label: qsTr("Label (optional)")
                // Closes the keyboard when the user presses Enter
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false

                validator: RegExpValidator {
                    regExp: /^$|^[a-zA-Z0-9+\-_]{3,}$/
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
                        enabled: !isBackupRunning
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
                text: isBackupRunning ? qsTr("Saving backup...") : qsTr("Start backup")
                anchors.horizontalCenter: parent.horizontalCenter
                // Disable the button while running so they can't spam it
                enabled: !noneSelected && !isBackupRunning && backupLabelField.acceptableInput
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

            ProgressBar {
                // width: parent.width
                width: parent.width - (Theme.horizontalPageMargin * 2)
                anchors.horizontalCenter: parent.horizontalCenter
                indeterminate: true
                visible: isBackupRunning
                opacity: isBackupRunning ? 1 : 0

                Behavior on opacity {
                    FadeAnimation {
                    }

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

    KeepAlive {
        // Prevents the system from suspending ONLY while a backup is running
        enabled: isBackupRunning
    }

}
