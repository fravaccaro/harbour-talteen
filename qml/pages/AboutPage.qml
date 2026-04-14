import "../components"
import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: aboutpage

    SilicaFlickable {
        id: flickable

        anchors.fill: parent
        contentHeight: content.height

        VerticalScrollDecorator {
        }

        Column {
            id: content

            width: parent.width

            PageHeader {
                title: qsTr("About Talteen")
            }

            Column {
                width: parent.width
                spacing: Theme.paddingMedium

                Item {
                    height: appicon.height + Theme.paddingMedium
                    width: parent.width

                    Image {
                        id: appicon

                        anchors.horizontalCenter: parent.horizontalCenter
                        source: "../../images/appinfo.png" // FIX 2: Responsive image sizing so it never overflows the screen
                        width: Math.min(512, parent.width - (Theme.paddingLarge * 2))
                        fillMode: Image.PreserveAspectFit
                        visible: status === Image.Ready
                    }

                }

                LabelSpacer {
                }

                LabelText {
                    text: qsTr("Back up and transfer app data, documents, downloaded files, music, pictures and videos on your Sailfish OS devices.")
                }

                LabelSpacer {
                }

                LabelText {
                    text: qsTr("Released under the <a href='https://github.com/fravaccaro/harbour-tarkka/blob/main/LICENSE'>GNU GPLv3</a> license.")
                }

                LabelSpacer {
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Sources")
                    onClicked: Qt.openUrlExternally("https://github.com/fravaccaro/harbour-talteen")
                }

                SectionHeader {
                    text: qsTr("Key features")
                }

                LabelText {
                    text: "<ul>" + "<li>" + qsTr("Back up Sailfish OS app data and files inside Documents, Downloads, Music, Pictures and Videos folders.") + "</li>" + "<li>" + qsTr("Back up to an SD card.") + " " + qsTr("Restore from a backup.") + ", " + qsTr("Over-the-air Wi-Fi transfer of backups.") + "</li>" + "</ul>"
                }

                SectionHeader {
                    text: qsTr("Feedback")
                }

                LabelText {
                    text: qsTr("If you want to provide feedback or report an issue, please use GitHub.")
                }

                LabelSpacer {
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Issues")
                    onClicked: Qt.openUrlExternally("https://github.com/fravaccaro/harbour-tarkka/issues")
                }

                SectionHeader {
                    text: qsTr("Support")
                }

                LabelText {
                    text: qsTr("If you like my work and want to buy me a beer, feel free to do it!")
                }

                LabelSpacer {
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Donate")
                    onClicked: Qt.openUrlExternally("https://www.paypal.me/fravaccaro")
                }

                LabelSpacer {
                }

                SectionHeader {
                    text: qsTr("Credits")
                }

                LabelText {
                    text: qsTr("Thanks to flypigahoy for his inspiring blog post about copying settings and files to a new device.")
                }

                LabelText {
                    text: qsTr("Thanks to jgibbon for the icon and the cover graphics.")
                }

                LabelText {
                    visible: false
                    text: qsTr("Apps backup logic by topias.")
                }

                LabelText {
                    text: qsTr("Thanks to all the testers for being brave and patient.")
                }

                SectionHeader {
                    text: qsTr("Translations")
                }

                DetailItem {
                    label: "Italiano"
                    value: "fravaccaro"
                }

                LabelText {
                    text: qsTr("Request a new language or contribute to existing languages.")
                }

                LabelSpacer {
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Transifex")
                    onClicked: Qt.openUrlExternally("https://explore.transifex.com/fravaccaro/talteen/")
                }

                LabelSpacer {
                }

            }

        }

    }

}
