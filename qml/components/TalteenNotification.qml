import Nemo.Notifications 1.0
import QtQuick 2.0

Notification {
    id: notificationComponent

    // Function to trigger the popup
    function show(title, message) {
        summary = title;
        body = message;
        progress = undefined;
        publish();
    }

    // PROGRESS MODE (Sticky, shows a bar)
    function updateProgress(title, message, progressValue) {
        summary = title;
        body = message;
        progress = progressValue;
        publish();
    }

    // Default settings that apply to all notifications
    appName: "Talteen"
    isTransient: false
}
