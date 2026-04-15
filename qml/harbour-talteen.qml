import Nemo.Notifications 1.0
import QtQuick 2.0
import Sailfish.Silica 1.0
import Nemo.Configuration 1.0
import "components"
import "pages"

ApplicationWindow {
    id: appWindow

    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

ConfigurationValue {
        id: disclaimerAccepted
        key: "/apps/harbour-talteen/disclaimer_accepted"
        defaultValue: false
    }

    property bool isLightTheme: (Theme.colorScheme === Theme.LightOnDark) ? false : true
    property bool isAppWorking: false
    property string appWorkingText: ""

    function showNotification(summary, body) {
        globalPopup.show(summary, body);
    }

    function showProgressNotification(summary, body, progressValue) {
        globalPopup.updateProgress(summary, body, progressValue);
    }

    function showToast(body) {
        globalPopup.toast(body);
    }


    TalteenNotification {
        id: globalPopup
    }

    initialPage: Component {
        MainPage {
        }

    }


Component.onCompleted: {
        if (!disclaimerAccepted.value) {
            var dialog = pageStack.push(Qt.resolvedUrl("pages/DisclaimerDialog.qml"));
            
            dialog.accepted.connect(function() {
                disclaimerAccepted.value = true;
            });
            
            dialog.rejected.connect(function() {
                Qt.quit();
            });
        }
}






}
