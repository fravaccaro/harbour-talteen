import Nemo.Notifications 1.0
import QtQuick 2.0
import Sailfish.Silica 1.0
import "components"
import "pages"

ApplicationWindow {
    id: appWindow

    property bool isLightTheme: (Theme.colorScheme === Theme.LightOnDark) ? false : true
    property bool isAppWorking: false
    property string appWorkingText: ""

    function showNotification(summary, body) {
        globalPopup.show(summary, body);
    }

    function showProgressNotification(summary, body, progressValue) {
        globalPopup.updateProgress(summary, body, progressValue);
    }

    cover: Qt.resolvedUrl("cover/CoverPage.qml")
    allowedOrientations: defaultAllowedOrientations

    TalteenNotification {
        id: globalPopup
    }

    initialPage: Component {
        MainPage {
        }

    }

}
