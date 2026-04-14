import "../components"
import Nemo.Notifications 1.0
import QtQuick 2.0
import Sailfish.Silica 1.0
import org.harbour.talteen 1.0

Page {
    id: restorePage

    property string selectedBackupPath: ""
    property string backupName: ""
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
            statusMessage = qsTr("Validating backup...");
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

                statusMessage = qsTr("Backup is valid");
                setAllSwitches(false);
            } else {
                statusMessage = qsTr("Error:") + " " + message;
                availableMetadata = ({
                });
                setAllSwitches(false);
            }
        }
        onRestoreFinished: {
            isRestoreRunning = false;
            statusMessage = message;
            console.log(message);
            appWindow.showNotification(success ? qsTr("Restore completed") : qsTr("Restore failed"), message);
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
                "name": qsTr("App order"),
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
        enabled: !isRestoreRunning
        opacity: isRestoreRunning ? 0.2 : 1
        anchors.fill: parent

        PullDownMenu {
            MenuItem {
                text: qsTr("Unselect all")
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

                StatusLabel {
                    text: qsTr("Status:") + " " + statusMessage
                }

                LabelSpacer {
                }

                SectionHeader {
                    text: qsTr("Details")
                }

                DetailItem {
                    label: qsTr("File")
                    value: backupName
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

            LabelSpacer {
            }

            Repeater {
                model: restoreCategoriesModel

                delegate: Column {
                    width: parent.width
                    visible: availableMetadata[model.key] === "true"

                    SectionHeader {
                        text: model.section
                        visible: model.section !== "" && isSectionVisible(model.triggers)
                    }

                    TextSwitch {
                        text: model.name
                        checked: model.isChecked
                        onCheckedChanged: {
                            if (model.isChecked !== checked) {
                                restoreCategoriesModel.setProperty(index, "isChecked", checked);
                                updateSelectionStates();
                            }
                        }
                    }

                }

            }

            LabelSpacer {
            }

            Button {
                id: restoreButton

                text: qsTr("Start restore")
                anchors.horizontalCenter: parent.horizontalCenter
                enabled: availableMetadata["version"] !== undefined && !noneSelected
                onClicked: {
                    isRestoreRunning = true;
                    var restoreOptions = {
                    };
                    for (var i = 0; i < restoreCategoriesModel.count; i++) {
                        var item = restoreCategoriesModel.get(i);
                        restoreOptions[item.key] = item.isChecked;
                    }
                    appCore.executeRestore(selectedBackupPath, restoreOptions);
                    appWindow.showProgressNotification(qsTr("Talteen Backup"), qsTr("Restoring archive..."), Notification.ProgressIndeterminate);
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

    Column {
        anchors.centerIn: parent
        width: parent.width - (Theme.paddingLarge * 2)
        visible: isRestoreRunning
        spacing: Theme.paddingMedium

        ProgressBar {
            width: parent.width
            indeterminate: true
            label: qsTr("Restoring backup...")
        }

    }

}
