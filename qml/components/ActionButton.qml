import QtQuick 2.0
import Sailfish.Silica 1.0

ButtonLayout {
    id: root

    property alias text: innerButton.text

    signal clicked()

    preferredWidth: Theme.buttonWidthMedium

    Button {
        id: innerButton

        opacity: enabled ? 1 : 0.3
        onClicked: root.clicked()
    }

}
