import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    id: cover

    property bool activeTask: appWindow.isAppWorking

    Image {
        id: coverimg

        fillMode: Image.PreserveAspectFit
        source: isLightTheme ? "../../images/coverbg-light.png" : "../../images/coverbg.png"
        opacity: cover.activeTask ? 0.1 : 0.4
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        height: sourceSize.height * width / sourceSize.width
    }

    Column {
        anchors.centerIn: parent
        width: parent.width
        spacing: Theme.paddingMedium
        visible: cover.activeTask

        Image {
            id: refreshimg

            source: "image://theme/graphic-busyindicator-large"
            anchors.horizontalCenter: parent.horizontalCenter
            fillMode: Image.PreserveAspectFit
            width: Theme.itemSizeLarge
            height: Theme.itemSizeLarge
            opacity: 0.6

            RotationAnimation on rotation {
                duration: 2000
                loops: Animation.Infinite
                running: cover.activeTask && cover.status === Cover.Active
                from: 0
                to: 360
            }

        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            // Reads the dynamic text, with a fallback just in case
            text: appWindow.appWorkingText !== "" ? appWindow.appWorkingText : qsTr("Just a moment...")
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
        }

    }

}
