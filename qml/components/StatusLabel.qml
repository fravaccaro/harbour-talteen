import QtQuick 2.0
import Sailfish.Silica 1.0

Label {
    id: statusLabel

    anchors.horizontalCenter: parent.horizontalCenter
    color: Theme.highlightColor
    font.pixelSize: Theme.fontSizeSmall
    wrapMode: Text.Wrap
    width: parent.width - (Theme.paddingLarge * 2)
    horizontalAlignment: Text.AlignHCenter
}
