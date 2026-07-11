import QtQuick
import QtQuick.Window
import QtWebChannel
import QtWebEngine
import TreesOfInsanity 1.0

Window {
    id: root
    width: 1280
    height: 800
    visible: false
    title: "Trees of Insanity"
    color: "#020617"

    Rectangle {
        anchors.fill: parent
        color: "#020617"
    }

    ViewportTextureItem {
        id: viewport
        objectName: "viewportTexture"
        z: 1
    }

    WebChannel {
        id: channel
        Component.onCompleted: registerObjects({ "desktopBridge": desktopBridge })
    }

    WebEngineView {
        id: web
        anchors.fill: parent
        z: 10
        url: uiUrl
        backgroundColor: "transparent"
        webChannel: channel
    }
}
