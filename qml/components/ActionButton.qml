import QtQuick 2.0
import Sailfish.Silica 1.0

ButtonLayout {
    id: root

    property alias text: innerButton.text
    property alias enabled: innerButton.enabled

    signal clicked()

    preferredWidth: Theme.buttonWidthMedium

    Button {
        id: innerButton

        onClicked: root.clicked()
    }

}
