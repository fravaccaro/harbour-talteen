import Nemo.Notifications 1.0
import QtQuick 2.0

Notification {
    id: notificationComponent

    // Function to trigger the popup
    function show(title, message) {
        summary = title;
        body = message;
        progress = undefined;
        isTransient = false;
        publish();
    }

    // Function to trigger the toast notification
    function toast(message) {
        summary = "";
        body = message;
        progress = undefined;
        isTransient = true;
        publish();
    }

    // PROGRESS MODE (Sticky, shows a bar)
    function updateProgress(title, message, progressValue) {
        summary = title;
        body = message;
        progress = progressValue;
        isTransient = false;
        publish();
    }

    // Default settings that apply to all notifications
    appName: "Talteen"
}
