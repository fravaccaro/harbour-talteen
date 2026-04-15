import QtQuick 2.0
import Sailfish.Silica 1.0

ProgressBar {
    width: parent.width - (Theme.paddingLarge * 2)
    anchors.horizontalCenter: parent.horizontalCenter

    Behavior on opacity {
        FadeAnimation {
        }

    }

}
