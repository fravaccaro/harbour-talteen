import "../components"
import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: technicalDetailsPage

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
                title: qsTr("Technical details")
            }

            Column {
                width: parent.width
                spacing: Theme.paddingMedium

SectionHeader {
                text: qsTr("Under the hood")
            }

            LabelText {
                text: qsTr("Talteen acts as a frontend for:")
            }

            LabelText {
                text: "<ul>" + 
                "<li>" + qsTr("Archiving:") + " <code>tar</code>" + "</li>" + 
                "<li>" + qsTr("Compression:") + " <code>xz</code>" + "</li>" + 
                "<li>" + qsTr("Encryption:") + " <code>openssl (AES-256-CBC with PBKDF2)</code>"+ "</li>" + "</ul>"
            }

            LabelText {
                text: qsTr("A custom <code>QTcpServer/QTcpSocket</code> implementation is used to facilitate device-to-device transfers over the local network.")
            }

                LabelSpacer {
                }
                
            SectionHeader {
                text: qsTr("Archive structure")
            }

            LabelText {
                text: qsTr("The app creates a standard tar archive with the extension <code>.talteen</code> holding two files:")

            }

            LabelText {
                text: "<ul>" + 
                "<li>" + "<code>payload.enc</code> " + qsTr("An AES-256 encrypted, XZ-compressed stream containing the actual files and folders.") + "</li>" +
                "<li>" + "<code>manifest.yaml</code> " + qsTr("A human-readable file containing the metadata necessary for the app to understand what is inside the encrypted payload without having to decrypt it first.") + "</li>" + "</ul>"

            }


                LabelSpacer {
                }

            SectionHeader {
                text: qsTr("Manual backup extraction")
            }

            LabelText {
                text: qsTr("The backup files are saved to:")
            }

            LabelText {
                text: "<ul>" +
                "<li>" + qsTr("Internal storage:") + " <code>/home/defaultuser/.local/share/harbour-talteen</code></li>" +
                "<li>" + qsTr("SD card:") + " <code>/run/media/sdcard/harbour-talteen</code>" + "</li>" + "</ul>"
            }


            LabelText {
                text: qsTr("You can extract any .talteen archive on any Linux terminal without using the app:")

            }

            LabelText {
                text: qsTr("Unpack the container:")
            }

TerminalBlock {
                command: "tar -xf backup_name.talteen"
            }

            LabelText {
                text: qsTr("Decrypt and unpack the payload:")
            }

TerminalBlock {
                command: "openssl enc -d -aes-256-cbc -pbkdf2 -in payload.enc | tar -xJv"
            }





                LabelSpacer {
                }

            }

        }

    }

}
