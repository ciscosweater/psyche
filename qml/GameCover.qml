import QtQuick
import QtQuick.Controls

Rectangle {
    id: cover
    property string appId: ""
    property string title: ""
    property bool active: true
    property bool dark: false
    property color accent: "#1762ba"
    readonly property bool ready: image.status === Image.Ready
    implicitWidth: 180; implicitHeight: 84
    color: dark ? "#242428" : "#e5edf8"
    radius: 4; clip: true
    function load() { if (active && appId.length > 0) artwork.request(appId) }
    onAppIdChanged: load()
    onActiveChanged: load()
    Component.onCompleted: load()
    Column {
        anchors.centerIn: parent; width: parent.width - 16; spacing: 4
        visible: !cover.ready
        Label { textFormat: Text.PlainText; text: cover.title ? cover.title.slice(0, 2).toUpperCase() : "P"; font.pixelSize: 25; font.bold: true; color: cover.accent; anchors.horizontalCenter: parent.horizontalCenter }
        Label { textFormat: Text.PlainText; text: artwork.states[cover.appId] === "loading" ? "Loading…" : "No image"; font.pixelSize: 11; color: cover.dark ? "#a1a1a8" : "#4e6581"; anchors.horizontalCenter: parent.horizontalCenter }
    }
    Image {
        id: image
        anchors.fill: parent
        source: cover.active && cover.appId ? (artwork.images[cover.appId] || "") : ""
        asynchronous: true; cache: true
        sourceSize.width: 460; sourceSize.height: 215
        fillMode: Image.PreserveAspectFit
        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
    }
    Accessible.role: Accessible.Graphic
    Accessible.name: cover.ready ? "Cover for " + cover.title : "Image unavailable for " + cover.title
}
