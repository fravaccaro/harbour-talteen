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
    property bool noneSelected: false

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
            appWindow.showNotification(success ? qsTr("Backup complete") : qsTr("Backup failed"), message);
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
                "isChecked": true
            });
            append({
                "key": "apporder",
                "name": qsTr("App grid layout"),
                "section": "",
                "isChecked": true
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

        PullDownMenu {
            enabled: !isBackupRunning
            busy: isBackupRunning

            MenuItem {
                text: "[WIP] " + qsTr("Deselect all")

                //enabled: !noneSelected
                enabled: false
                onClicked: setAllSwitches(false)
            }

            MenuItem {
                text: "[WIP] " + qsTr("Select all")
                //enabled: !allSelected
                enabled: false
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
                label: (text.length > 0 && !acceptableInput) ? qsTr("Invalid label (min 3 chars, no spaces)") : qsTr("Label (optional)")
                errorHighlight: text.length > 0 && !acceptableInput
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: passwordField.focus = true

                validator: RegExpValidator {
                    regExp: /^$|^[a-zA-Z0-9+\-_ ]{3,}$/
                }

            }

            PasswordField {
                id: passwordField

                enabled: !isBackupRunning
                width: parent.width
                label: (text.length > 0 && text.length < 6) ? qsTr("Password (min 6 characters)") : qsTr("Password")
                placeholderText: qsTr("Enter a password")
                errorHighlight: text.length > 0 && text.length < 6
                EnterKey.enabled: text.length >= 6
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: repeatPasswordField.focus = true
            }

            PasswordField {
                id: repeatPasswordField

                enabled: !isBackupRunning
                width: parent.width
                // Dynamically warn the user if the passwords don't match
                label: (text.length > 0 && text !== passwordField.text) ? qsTr("Passwords do not match") : qsTr("Repeat password")
                placeholderText: qsTr("Re-enter password")
                errorHighlight: text.length > 0 && text !== passwordField.text
                // Prevent from closing the keyboard if the passwords don't match
                EnterKey.enabled: text === passwordField.text
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
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
                                categoriesModel.setProperty(index, "isChecked", checked);
                                updateSelectionStates();
                        }
                    }

                }

            }

            LabelSpacer {
            }

            ActionButton {
                text: isBackupRunning ? qsTr("Saving backup...") : qsTr("Start backup")
                // Disable the button while running so they can't spam it
                enabled: !noneSelected && !isBackupRunning && backupLabelField.acceptableInput && passwordField.text.length > 0 && passwordField.text === repeatPasswordField.text
                onClicked: {
                    isBackupRunning = true;
                    var backupOptions = {
                        "destination": storageBtn.saveToSdCard ? appCore.getSdCardPath() : "internal",
                        "label": backupLabelField.text,
                        "password": passwordField.text
                    };
                    for (var i = 0; i < categoriesModel.count; i++) {
                        var item = categoriesModel.get(i);
                        backupOptions[item.key] = item.isChecked;
                    }
                    appCore.startBackup(backupOptions);
                    appWindow.showProgressNotification(qsTr("Backup"), qsTr("Saving backup..."), Notification.ProgressIndeterminate);
                }
            }

            ProgressStatusBar {
                indeterminate: true
                enabled: isBackupRunning
                opacity: isBackupRunning ? 1 : 0
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
