import QtQuick 2.0
import Sailfish.Silica 1.0

Rectangle {
    id: root

    property string command: ""

    x: Theme.horizontalPageMargin
    width: parent.width - (Theme.horizontalPageMargin * 2)
    height: cmdLabel.height + (Theme.paddingMedium * 2)
    color: Theme.rgba(Theme.highlightDimmerColor, 0.8)
    radius: Theme.paddingSmall

    Label {
        id: cmdLabel

        x: Theme.horizontalPageMargin
        width: parent.width - (Theme.horizontalPageMargin * 2)
        anchors.verticalCenter: parent.verticalCenter
        text: root.command
        font.family: "monospace"
        color: Theme.highlightColor
        font.pixelSize: Theme.fontSizeExtraSmall
        wrapMode: Text.WrapAnywhere
    }

}
