import "../components"
import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: technicalDetailsPage

    allowedOrientations: Orientation.All

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
                    text: "<ul>" + "<li>" + qsTr("Archiving:") + " <code>tar</code>" + "</li>" + "<li>" + qsTr("Compression:") + " <code>xz</code>" + "</li>" + "<li>" + qsTr("Encryption (v2 default):") + " <code>openssl EVP (AES-256-GCM with PBKDF2-HMAC-SHA256)</code>" + "</li>" + "<li>" + qsTr("Encryption (v1 legacy):") + " <code>openssl enc (AES-256-CBC with PBKDF2)</code>" + "</li>" + "</ul>"
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
                    text: "<ul>" + "<li><code>payload.enc</code> " + qsTr("an encrypted, XZ-compressed stream containing the actual files and folders.") + "</li>" + "<li><code>manifest.yaml</code> " + qsTr("a human-readable file containing metadata used to detect the backup format and required decryption parameters.") + "</li>" + "</ul>"
                }

                LabelText {
                    text: qsTr("V2 backups (default) use AES-256-GCM and store <code>kdf_iterations</code>, <code>salt_b64</code>, <code>iv_b64</code>, <code>tag_b64</code>, and <code>aad</code> in the manifest.")
                }

                LabelText {
                    text: qsTr("V1 backups are still supported for restore and use AES-256-CBC with a separate SHA-256 checksum in the manifest.")
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
                    text: "<ul>" + "<li>" + qsTr("Internal storage:") + " <code>/home/defaultuser/.local/share/harbour-talteen</code></li>" + "<li>" + qsTr("SD card:") + " <code>/run/media/sdcard/harbour-talteen</code>" + "</li>" + "</ul>"
                }

                LabelText {
                    text: qsTr("You can extract legacy v1 <code>.talteen</code> archives on any Linux terminal without using the app:")
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

                LabelText {
                    text: qsTr("For v2 backups, use the helper script from this repository:")
                }

                ActionButton {
                    text: qsTr("Script")
                    onClicked: Qt.openUrlExternally("https://github.com/fravaccaro/harbour-talteen/blob/main/scripts/decrypt-v2.sh")
                }

                TerminalBlock {
                    command: "scripts/decrypt-v2.sh -i backup_name.talteen -o restored-data"
                }

                LabelSpacer {
                }

            }

        }

    }

}
