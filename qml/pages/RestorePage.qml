import "../components"
import Nemo.KeepAlive 1.2
import Nemo.Notifications 1.0
import QtQuick 2.0
import Sailfish.Silica 1.0
import org.harbour.talteen 1.0

Page {
    id: restorePage

    property string selectedBackupPath: ""
    property string backupLabel: ""
    property string backupDate: ""
    property string backupSize: ""
    property var availableMetadata: ({
    })
    property bool isRestoreRunning: false
    property string statusMessage
    property bool allSelected: false
    property bool noneSelected: true

    function setAllSwitches(state) {
        for (var i = 0; i < restoreCategoriesModel.count; i++) {
            restoreCategoriesModel.setProperty(i, "isChecked", state);
        }
        updateSelectionStates();
    }

    function updateSelectionStates() {
        var all = true;
        var none = true;
        for (var i = 0; i < restoreCategoriesModel.count; i++) {
            // Check only the items that are actually present in the backup
            if (availableMetadata[restoreCategoriesModel.get(i).key] === "true") {
                if (restoreCategoriesModel.get(i).isChecked)
                    none = false;
                else
                    all = false;
            }
        }
        allSelected = all;
        noneSelected = none;
    }

    function isSectionVisible(keysString) {
        if (!keysString)
            return false;

        var keys = keysString.split(",");
        for (var i = 0; i < keys.length; i++) {
            if (availableMetadata[keys[i]] === "true")
                return true;

        }
        return false;
    }

    allowedOrientations: Orientation.All
    backNavigation: !isRestoreRunning
    onIsRestoreRunningChanged: {
        appWindow.isAppWorking = isRestoreRunning;
        appWindow.appWorkingText = qsTr("Restoring backup...");
    }
    Component.onCompleted: {
        if (selectedBackupPath !== "") {
            statusMessage = qsTr("Checking backup...");
            appCore.analyzeArchive(selectedBackupPath);
        }
    }

    Talteen {
        id: appCore

        onArchiveAnalyzed: {
            if (isValid) {
                availableMetadata = metadata;
                // If not provided by MainPage, grab them from the archive
                if (backupLabel === "")
                    backupLabel = metadata["label"] ? metadata["label"] : qsTr("None");

                if (backupDate === "")
                    backupDate = metadata["time"] ? metadata["time"] : qsTr("Unknown");

                statusMessage = qsTr("Ready");
                appWindow.showToast(qsTr("Backup is ready"));
                setAllSwitches(false);
            } else {
                statusMessage = qsTr("Error: %1").arg(message);
                availableMetadata = ({
                });
                setAllSwitches(false);
            }
        }
        
onProgressUpdate: {
    restorePage.statusMessage = statusMessage;
            
            appWindow.appWorkingText = statusMessage;
            
            appWindow.showProgressNotification(qsTr("Restore in progress"), statusMessage, Notification.ProgressIndeterminate);
        }

        onRestoreFinished: {
            isRestoreRunning = false;
            statusMessage = message;
            console.log(message);
            appWindow.showNotification(success ? qsTr("Restore complete") : qsTr("Restore failed"), message);
        }
    }

    // Categories model
    ListModel {
        id: restoreCategoriesModel

        Component.onCompleted: {
            // 'triggers' is used to determine whether the section header should be visible.
            append({
                "key": "appdata",
                "name": qsTr("App data"),
                "section": qsTr("Applications"),
                "triggers": "appdata,apporder",
                "isChecked": false
            });
            append({
                "key": "apporder",
                "name": qsTr("App grid layout"),
                "section": "",
                "triggers": "",
                "isChecked": false
            });
            append({
                "key": "calls",
                "name": qsTr("Call history"),
                "section": qsTr("Communication"),
                "triggers": "calls,messages",
                "isChecked": false
            });
            append({
                "key": "messages",
                "name": qsTr("Messages"),
                "section": "",
                "triggers": "",
                "isChecked": false
            });
            append({
                "key": "documents",
                "name": qsTr("Documents"),
                "section": qsTr("Files"),
                "triggers": "documents,downloads",
                "isChecked": false
            });
            append({
                "key": "downloads",
                "name": qsTr("Downloads"),
                "section": "",
                "triggers": "",
                "isChecked": false
            });
            append({
                "key": "pictures",
                "name": qsTr("Pictures"),
                "section": qsTr("Media"),
                "triggers": "pictures,videos,music",
                "isChecked": false
            });
            append({
                "key": "videos",
                "name": qsTr("Videos"),
                "section": "",
                "triggers": "",
                "isChecked": false
            });
            append({
                "key": "music",
                "name": qsTr("Music"),
                "section": "",
                "triggers": "",
                "isChecked": false
            });
        }
    }

    SilicaFlickable {
        id: restoreView

        contentHeight: column.height + Theme.paddingLarge
        anchors.fill: parent

        PullDownMenu {
            enabled: !isRestoreRunning
            busy: isRestoreRunning

            MenuItem {
                text: qsTr("Deselect all")
                enabled: availableMetadata["version"] !== undefined && !noneSelected
                onClicked: setAllSwitches(false)
            }

            MenuItem {
                text: qsTr("Select all")
                enabled: availableMetadata["version"] !== undefined && !allSelected
                onClicked: setAllSwitches(true)
            }

        }

        Column {
            id: column

            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Restore")
            }

            Column {
                width: parent.width
                visible: selectedBackupPath !== ""



                LabelInfo {
                    text: qsTr("Enter the backup password and select the items you want to restore.")
                }

            LabelSpacer {
            }
                SectionHeader {
                    text: qsTr("Details")
                }

                DetailItem {
                    label: qsTr("Label")
                    value: backupLabel
                }

                DetailItem {
                    label: qsTr("Date")
                    value: backupDate
                }

                DetailItem {
                    label: qsTr("Size")
                    value: backupSize
                    visible: backupSize !== ""
                }

            }




SectionHeader {
                text: qsTr("Security")
            }
            PasswordField {
                id: restorePasswordField

                width: parent.width
                enabled: !isRestoreRunning
                // Only show the field if the backup is actually valid
                visible: availableMetadata["version"] !== undefined
                label: qsTr("Password")
                placeholderText: qsTr("Enter the backup password")
                errorHighlight: text.length > 0 && text.length < 6
                EnterKey.enabled: text.length >= 6
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            LabelSpacer {
            }
            Repeater {
                model: restoreCategoriesModel

                delegate: Column {
                    width: parent.width
                    visible: availableMetadata[model.key] === "true" || (model.section !== "" && isSectionVisible(model.triggers))

                    SectionHeader {
                        text: model.section
                        visible: model.section !== "" && isSectionVisible(model.triggers)
                    }

                    TextSwitch {
                        visible: availableMetadata[model.key] === "true"
                        text: model.name
                        description: model.key === "apporder" ? qsTr("Takes effect after restarting the home screen or device") : ""
                        enabled: !isRestoreRunning
                        checked: model.isChecked
                        onClicked: {
                            restoreCategoriesModel.setProperty(index, "isChecked", checked);
                            checked = Qt.binding(function() {
                                return model.isChecked;
                            });
                            updateSelectionStates();
                        }
                    }

                }

            }

            LabelSpacer {
            }

            ActionButton {
                id: restoreButton

                text: isRestoreRunning ? qsTr("Restoring...") : qsTr("Start restore")
                enabled: availableMetadata["version"] !== undefined && !noneSelected && !isRestoreRunning && restorePasswordField.text.length > 5
                onClicked: {
                    isRestoreRunning = true;
                    var restoreOptions = {
                        "password": restorePasswordField.text
                    };
                    for (var i = 0; i < restoreCategoriesModel.count; i++) {
                        var item = restoreCategoriesModel.get(i);
                        restoreOptions[item.key] = item.isChecked;
                    }
                    
                    appWindow.showProgressNotification(qsTr("Restore in progress"), qsTr("Preparing restore..."), Notification.ProgressIndeterminate);
                    appCore.executeRestore(selectedBackupPath, restoreOptions);
                }
            }


            LabelSpacer {
            }
            
                StatusLabel {
                    text: statusMessage
                }

            ProgressStatusBar {
                indeterminate: true
                enabled: isRestoreRunning
                opacity: isRestoreRunning ? 1 : 0
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
        enabled: isRestoreRunning
    }

}
