import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: window
    objectName: "psycheWindow"
    width: 960; height: 720
    minimumWidth: 620; minimumHeight: 580
    visible: true; title: "psyche"
    readonly property bool dark: true
    readonly property color canvas: "#111112"
    readonly property color surface: "#19191b"
    readonly property color raised: "#232326"
    readonly property color ink: "#e0e0e4"
    readonly property color muted: "#b0b0b8"
    readonly property color border: "#45454d"
    readonly property color accent: "#c4c4ca"
    readonly property int tabGames: 0
    readonly property int tabImport: 1
    readonly property int tabLibrary: 2
    readonly property int tabHistory: 3
    readonly property int tabSettings: 4
    color: canvas
    font.family: "Sans Serif"; font.pixelSize: 14
    palette.window: canvas; palette.base: surface; palette.text: ink
    palette.windowText: ink; palette.button: raised; palette.buttonText: ink
    palette.highlight: "#66666e"; palette.highlightedText: "#ffffff"
    palette.placeholderText: muted; palette.mid: border; palette.light: border; palette.dark: canvas
    property string removingId: ""
    property string removingName: ""
    property bool hydrated: false
    property bool searched: false
    property bool needsKey: false
    property bool dropHover: false
    property string notice: ""
    property string lastSearch: preferences.lastQuery
    property string pendingAppId: ""
    property string pendingName: ""
    property int restoreIndex: -1
    property string restoreDestination: ""
    FontLoader { id: pixel; objectName: "pixelFont"; source: "fonts/PixelifySans.ttf" }
    Component.onCompleted: { width = preferences.windowWidth; height = preferences.windowHeight; hydrated = true }
    onClosing: function(close) {
        if (backend.busy) {
            close.accepted = false
            window.notice = "Wait until the current task finishes."
            return
        }
        preferences.saveWindow(width, height)
    }
    function contentLabel() {
        const i = ["full", "basegame", "dlc", "zip"].indexOf(preferences.downloadContent)
        return ["Game + DLCs", "Base game only", "DLCs only", "Full ZIP"][Math.max(0, i)]
    }
    function promptKey() {
        window.needsKey = true
        tabs.currentIndex = window.tabSettings
    }
    function requestFetch(appId, name) {
        if (!preferences.hasApiKey) { promptKey(); return }
        window.pendingAppId = appId
        window.pendingName = name
        fetchDialog.open()
    }
    function runSearch(offset) {
        if (!preferences.hasApiKey) { promptKey(); return }
        window.needsKey = false
        if (offset === 0) lastSearch = query.text.trim()
        if (/^[0-9]+$/.test(lastSearch)) { requestFetch(lastSearch, ""); return }
        searched = true
        backend.search(lastSearch, offset)
    }
    function acceptZip(url) {
        const path = String(url)
        if (!path.toLowerCase().endsWith(".zip")) {
            window.notice = "Drop a single ZIP file."
            return false
        }
        window.notice = ""
        backend.inspect(url)
        return true
    }
    function pathIndex(items, path) {
        for (let i = 0; i < items.length; ++i) if (items[i].path === path) return i
        return -1
    }
    function formatWhen(value) {
        const date = new Date(value)
        return isNaN(date.getTime()) ? "" : Qt.locale().toString(date, Locale.ShortFormat)
    }
    Connections { target: preferences; function onChanged() { if (tabs.currentIndex === window.tabLibrary) backend.refreshLibrary() } }
    Connections { target: backend; function onPackageLoaded() { tabs.currentIndex = window.tabImport } }
    Connections {
        target: backend
        function onChanged() { if (!backend.busy && window.notice === "Wait until the current task finishes.") window.notice = "" }
    }

    component PixelText: Label { textFormat: Text.PlainText; font.family: pixel.name; font.pixelSize: 26; color: window.ink; wrapMode: Text.WordWrap }
    component Hint: Label { textFormat: Text.PlainText; color: window.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true; font.pixelSize: 12 }
    component Action: Button {
        id: action
        property bool primary: false
        implicitHeight: 34; leftPadding: 12; rightPadding: 12
        hoverEnabled: true
        contentItem: Text {
            text: action.text; font: action.font
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            color: !action.enabled ? "#85858c" : action.primary ? "#171719" : window.ink
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 4
            color: !action.enabled ? "#242427" : action.primary ? (action.hovered ? "#e1e1e5" : window.accent) : action.hovered || action.down || action.checked ? "#35353d" : action.flat ? "transparent" : window.raised
            border.width: action.activeFocus ? 2 : 1
            border.color: action.activeFocus ? window.ink : action.primary && action.enabled ? window.accent : window.border
        }
    }
    component Input: TextField {
        implicitHeight: 36; leftPadding: 10; rightPadding: 10
        color: window.ink; placeholderTextColor: window.muted
        selectByMouse: true
        background: Rectangle { color: window.surface; radius: 4; border.width: parent.activeFocus ? 2 : 1; border.color: parent.activeFocus ? window.accent : window.border }
    }
    component PathField: Input { readOnly: true; Layout.fillWidth: true; font.pixelSize: 12; Accessible.name: "Path" }
    component Card: Pane {
        padding: 12
        contentHeight: contentItem.children.length > 0 ? contentItem.children[0].implicitHeight : 0
        background: Rectangle { color: window.surface; radius: 6; border.color: window.border }
    }
    component ChoiceBox: ComboBox {
        id: box
        enabled: !backend.busy; implicitHeight: 40
        contentItem: Text {
            text: box.displayText; color: box.enabled ? window.ink : window.muted
            verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
            leftPadding: 12; rightPadding: 28; font.pixelSize: 13
        }
        background: Rectangle { color: window.raised; radius: 4; border.color: box.activeFocus ? window.ink : window.border }
        indicator: Text { text: "⌄"; color: window.ink; font.pixelSize: 20; x: box.width - width - 12; anchors.verticalCenter: parent.verticalCenter }
        delegate: ItemDelegate {
            required property int index
            required property var modelData
            width: box.width; highlighted: box.highlightedIndex === index
            contentItem: Text {
                text: box.textRole && modelData && typeof modelData === "object" ? (modelData[box.textRole] || "") : String(modelData)
                color: window.ink; elide: Text.ElideRight; leftPadding: 8; font.pixelSize: 13
            }
            background: Rectangle { color: highlighted ? window.surface : window.raised }
        }
        popup: Popup {
            y: box.height + 2; width: box.width; padding: 1
            implicitHeight: Math.min(240, contentItem.implicitHeight + 2)
            contentItem: ListView {
                clip: true; implicitHeight: contentHeight
                model: box.popup.visible ? box.delegateModel : null
                currentIndex: box.highlightedIndex
                ScrollBar.vertical: ScrollBar {}
            }
            background: Rectangle { color: window.raised; radius: 4; border.color: window.border }
        }
    }
    component ContentChoice: ChoiceBox {
        model: ["Game + DLCs", "Base game only", "DLCs only", "Full ZIP"]
        currentIndex: ["full", "basegame", "dlc", "zip"].indexOf(preferences.downloadContent)
        onActivated: preferences.setDownloadContent(["full", "basegame", "dlc", "zip"][currentIndex])
        Accessible.name: "Content to download from Hubcap"
        ToolTip.visible: hovered; ToolTip.text: "Each download uses Hubcap daily quota."
    }
    component Tick: CheckBox {
        id: tick
        indicator: Rectangle {
            implicitWidth: 18; implicitHeight: 18
            x: tick.leftPadding; y: parent.height / 2 - height / 2
            radius: 3; color: window.surface
            border.color: tick.activeFocus ? window.ink : window.border
            Rectangle {
                anchors.centerIn: parent; width: 10; height: 10; radius: 2
                color: window.accent; visible: tick.checked
            }
        }
        contentItem: Text {
            text: tick.text; color: window.ink; font.pixelSize: 13
            leftPadding: tick.indicator.width + 8; verticalAlignment: Text.AlignVCenter
        }
    }
    component NavTab: TabButton {
        id: nav
        implicitHeight: 38
        contentItem: Text {
            text: nav.text; font.family: pixel.name
            font.pixelSize: window.width < 720 ? 15 : 18
            color: nav.checked ? window.ink : window.muted
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: nav.hovered || nav.checked ? window.surface : "transparent"
            border.color: nav.activeFocus ? window.muted : "transparent"
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 2; color: window.accent; visible: nav.checked }
        }
    }
    component Sheet: Dialog {
        id: sheet
        modal: true; parent: Overlay.overlay; anchors.centerIn: parent; focus: true
        width: Math.min(window.width - 32, 480)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        palette.window: window.surface; palette.windowText: window.ink
        background: Rectangle { color: window.surface; radius: 6; border.color: window.border }
        header: Label {
            text: sheet.title; font.family: pixel.name; font.pixelSize: 20; color: window.ink
            padding: 16; bottomPadding: 8; wrapMode: Text.WordWrap
        }
        padding: 16
    }
    header: Pane {
        padding: 0; background: Rectangle { color: window.canvas }
        ColumnLayout {
            anchors.fill: parent; spacing: 12
            RowLayout {
                Layout.fillWidth: true; Layout.topMargin: 16; Layout.leftMargin: 16; Layout.rightMargin: 16
                PixelText { text: "psyche_"; font.pixelSize: 30; Accessible.name: "psyche" }
                Item { Layout.fillWidth: true }
                Action {
                    text: preferences.hasApiKey ? "Hubcap" : "+ Connect Hubcap"
                    flat: true; onClicked: tabs.currentIndex = window.tabSettings
                    Accessible.name: "Configure Hubcap connection"
                }
            }
            TabBar {
                id: tabs; objectName: "mainTabs"
                Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                currentIndex: preferences.lastTab
                onCurrentIndexChanged: if (window.hydrated) preferences.saveNavigation(currentIndex, window.lastSearch)
                background: Item {}
                NavTab { text: "Games"; objectName: "searchTab" }
                NavTab { text: "Import"; objectName: "importTab" }
                NavTab { text: "Library"; objectName: "libraryTab" }
                NavTab { text: "History"; objectName: "historyTab" }
                NavTab { text: "Settings"; objectName: "settingsTab" }
            }
        }
    }
    StackLayout {
        anchors.fill: parent; currentIndex: tabs.currentIndex
        Pane {
            padding: 16; background: Item {}
            ColumnLayout {
                anchors.fill: parent; spacing: 10
                RowLayout {
                    Layout.fillWidth: true; spacing: 10
                    Input {
                        id: query; objectName: "searchQuery"; Layout.fillWidth: true
                        Component.onCompleted: text = preferences.lastQuery
                        placeholderText: "Game name or AppID"
                        enabled: !backend.busy; Accessible.name: "Game name or AppID"
                        onAccepted: if (text.trim()) window.runSearch(0)
                    }
                    Action { text: "Search"; primary: true; enabled: !backend.busy && query.text.trim().length > 0; onClicked: window.runSearch(0) }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ContentChoice { Layout.preferredWidth: 180 }
                    Hint {
                        text: preferences.hasApiKey ? "Uses Hubcap quota" : "Connect Hubcap in Settings to search."
                        horizontalAlignment: Text.AlignRight
                    }
                }
                ListView {
                    id: results; objectName: "searchResults"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; spacing: 10; model: backend.games
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ItemDelegate {
                        id: result
                        required property var modelData
                        width: results.width; height: window.width < 820 ? 94 : 116
                        enabled: !backend.busy; padding: 12
                        background: Rectangle { radius: 5; color: result.hovered ? window.raised : window.surface; border.color: result.activeFocus ? window.ink : window.border }
                        contentItem: RowLayout {
                            spacing: 10
                            GameCover {
                                appId: modelData.appId; title: modelData.name; active: tabs.currentIndex === window.tabGames
                                dark: true; accent: window.accent
                                Layout.preferredWidth: window.width < 820 ? 120 : 180
                                Layout.preferredHeight: window.width < 820 ? 56 : 84
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 6
                                Label { textFormat: Text.PlainText; text: modelData.name; font.pixelSize: 16; color: window.ink; elide: Text.ElideRight; Layout.fillWidth: true }
                                Hint { text: "#" + modelData.appId }
                            }
                            Label { textFormat: Text.PlainText; text: "→"; font.pixelSize: 22; color: window.muted }
                        }
                        onClicked: window.requestFetch(modelData.appId, modelData.name)
                        Accessible.name: "Download " + modelData.name + ", AppID " + modelData.appId + ", uses Hubcap quota"
                    }
                    Column {
                        anchors.centerIn: parent; width: parent.width - 32; spacing: 10
                        visible: results.count === 0 && !backend.busy && results.height > 90
                        PixelText { text: window.searched ? "Nothing here." : "Which game?"; font.pixelSize: 30; anchors.horizontalCenter: parent.horizontalCenter }
                        Label { textFormat: Text.PlainText; text: window.searched ? "Try another name." : "Search above or open a ZIP."; color: window.muted; anchors.horizontalCenter: parent.horizontalCenter }
                    }
                    BusyIndicator { anchors.centerIn: parent; running: backend.activity === "search"; visible: running; palette.dark: window.accent }
                }
                RowLayout {
                    visible: backend.games.length > 0 || backend.hasMore || backend.searchOffset > 0
                    Layout.fillWidth: true
                    Action { text: "Previous"; Accessible.name: "Previous page"; enabled: !backend.busy && backend.searchOffset > 0; onClicked: window.runSearch(Math.max(0, backend.searchOffset - 100)) }
                    Hint { text: "Page " + (Math.floor(backend.searchOffset / 100) + 1); horizontalAlignment: Text.AlignHCenter }
                    Action { text: "Next"; Accessible.name: "Next page"; enabled: !backend.busy && backend.hasMore; onClicked: window.runSearch(backend.searchOffset + 100) }
                }
            }
        }
        Pane {
            padding: 16; background: Item {}
            ColumnLayout {
                anchors.fill: parent; spacing: 10
                RowLayout {
                    Layout.fillWidth: true
                    PixelText { text: backend.ready ? "Ready to add." : "Import ZIP"; Layout.fillWidth: true }
                    Action { text: "Open ZIP"; enabled: !backend.busy; onClicked: zipDialog.open() }
                }
                ScrollView {
                    id: importScroll; objectName: "importPage"
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; contentWidth: width
                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }
                    ColumnLayout {
                        id: importContent
                        width: importScroll.width; spacing: 10
                        Card {
                            objectName: "zipCard"
                            visible: !backend.ready
                            Layout.fillWidth: true; contentHeight: 164
                            background: Rectangle {
                                color: window.surface; radius: 6
                                border.color: window.dropHover ? window.ink : window.border
                                border.width: window.dropHover ? 2 : 1
                            }
                            Column {
                                width: parent.width; anchors.verticalCenter: parent.verticalCenter; spacing: 12
                                PixelText { text: "+ ZIP"; font.pixelSize: 32; anchors.horizontalCenter: parent.horizontalCenter }
                                Hint { width: parent.width; text: backend.activity === "import" ? "Preparing…" : "Drop your file here."; horizontalAlignment: Text.AlignHCenter }
                                BusyIndicator { running: backend.activity === "import"; visible: running; anchors.horizontalCenter: parent.horizontalCenter; palette.dark: window.accent }
                            }
                            DropArea {
                                anchors.fill: parent
                                onEntered: function(drag) { window.dropHover = !backend.busy; drag.accepted = !backend.busy }
                                onExited: window.dropHover = false
                                onDropped: function(drop) {
                                    window.dropHover = false
                                    if (backend.busy) return
                                    if (!drop.hasUrls || drop.urls.length !== 1) {
                                        window.notice = "Drop a single ZIP file."
                                        return
                                    }
                                    if (window.acceptZip(drop.urls[0]))
                                        drop.acceptProposedAction()
                                }
                            }
                        }
                        Card {
                            objectName: "readyCard"
                            visible: backend.ready; Layout.fillWidth: true
                            ColumnLayout {
                                width: parent.width; spacing: 10
                                RowLayout {
                                    Layout.fillWidth: true; spacing: 10
                                    GameCover {
                                        visible: backend.appId.length > 0; active: visible && tabs.currentIndex === window.tabImport
                                        appId: backend.appId; title: backend.source; dark: true; accent: window.accent
                                        Layout.preferredWidth: 140
                                        Layout.preferredHeight: 65
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 10
                                        Label { textFormat: Text.PlainText; text: backend.source; font.pixelSize: 16; color: window.ink; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                                        Hint { text: backend.counts.apps + " apps  ·  " + backend.counts.depots + " depots  ·  " + backend.counts.keys + " keys" }
                                    }
                                }
                                Input { Layout.fillWidth: true; text: backend.gameName; placeholderText: "Game name in comments"; Accessible.name: "Game name in YAML comments"; enabled: !backend.busy && !backend.applied; onTextEdited: backend.gameName = text }
                                Action { id: details; objectName: "entriesToggle"; text: checked ? "− Hide entries" : "+ View entries"; checkable: true; flat: true; Accessible.name: "Show package entries" }
                                ScrollView {
                                    visible: details.checked; Layout.fillWidth: true; Layout.preferredHeight: Math.min(140, entriesText.implicitHeight)
                                    TextArea { id: entriesText; text: backend.preview; padding: 8; background: Rectangle { color: window.canvas; radius: 4 } readOnly: true; wrapMode: TextEdit.Wrap; selectByMouse: true; font.family: "monospace"; font.pixelSize: 12; color: window.ink; Accessible.name: "Package entries" }
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 4
                        Label { textFormat: Text.PlainText; text: "config.yaml"; color: window.ink; font.pixelSize: 13 }
                        Label { textFormat: Text.PlainText; text: preferences.destination || "Choose SLSsteam directory"; color: window.muted; font.pixelSize: 12; elide: Text.ElideLeft; Layout.fillWidth: true }
                    }
                    Action { text: "Change"; flat: true; enabled: !backend.busy; onClicked: destinationDialog.open() }
                }
                RowLayout {
                    Layout.fillWidth: true; spacing: 12
                    Hint { visible: backend.ready && !preferences.destinationValid; text: "Choose an SLSsteam directory." }
                    Hint { visible: backend.applied; text: "Backup saved in History." }
                    Action {
                        visible: backend.applied
                        text: "Open Library"
                        onClicked: tabs.currentIndex = window.tabLibrary
                    }
                    Action {
                        objectName: "applyButton"
                        text: backend.applied ? "Added ✓" : backend.activity === "apply" ? "Adding…" : "Add to config.yaml"
                        primary: true
                        enabled: backend.ready && preferences.destinationValid && !backend.busy && !backend.applied
                        onClicked: backend.apply()
                    }
                }
            }
        }
        Pane {
            padding: 16; background: Item {}
            ColumnLayout {
                anchors.fill: parent; spacing: 10
                RowLayout {
                    PixelText { text: "Library"; Layout.fillWidth: true }
                    Action { text: "Refresh"; enabled: !backend.busy; onClicked: backend.refreshLibrary() }
                }
                Hint { text: backend.libraryError || "Games in config.yaml" }
                ListView {
                    id: installedList; objectName: "installedList"
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 8
                    model: backend.installed; ScrollBar.vertical: ScrollBar {}
                    delegate: Card {
                        required property var modelData
                        width: installedList.width
                        Accessible.name: modelData.name + ", " + modelData.apps + " apps"
                        RowLayout {
                            width: parent.width; spacing: 10
                            GameCover { appId: modelData.appId; title: modelData.name; active: tabs.currentIndex === window.tabLibrary; dark: true; accent: window.accent; Layout.preferredWidth: 100; Layout.preferredHeight: 47 }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 4
                                Label { textFormat: Text.PlainText; text: modelData.name; color: window.ink; elide: Text.ElideRight; Layout.fillWidth: true }
                                Hint { text: modelData.apps + " apps · " + modelData.depots + " depots · " + modelData.keys + " keys" }
                            }
                            Action { text: "Remove"; enabled: !backend.busy; Accessible.name: "Remove " + modelData.name; onClicked: { window.removingId = modelData.id; window.removingName = modelData.name; preferences.refreshSteamRunning(); removeDialog.open() } }
                        }
                    }
                    PixelText { anchors.centerIn: parent; visible: installedList.count === 0 && !backend.libraryError; text: "No configured games."; font.pixelSize: 26 }
                }
            }
        }
        Pane {
            padding: 16; background: Item {}
            ColumnLayout {
                anchors.fill: parent; spacing: 12
                PixelText { text: "History" }
                ListView {
                    id: historyList; objectName: "historyList"
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 10; model: preferences.history
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Card {
                        required property var modelData
                        required property int index
                        width: historyList.width
                        Accessible.name: modelData.source + (modelData.restored ? ", restored" : ", added")
                        ColumnLayout {
                            width: parent.width; spacing: 14
                            RowLayout {
                                Layout.fillWidth: true; spacing: 14
                                GameCover {
                                    visible: !!modelData.appId; active: visible && tabs.currentIndex === window.tabHistory
                                    appId: modelData.appId || ""; title: modelData.source; dark: true; accent: window.accent
                                    Layout.preferredWidth: 100; Layout.preferredHeight: 47
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 6
                                    Label { textFormat: Text.PlainText; text: modelData.source; color: window.ink; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Hint { text: window.formatWhen(modelData.date) + (modelData.restored ? "  ·  Restored" : "  ·  Added") }
                                }
                            }
                            RowLayout {
                                Action { text: "Restore"; enabled: !backend.busy && !modelData.restored; Accessible.name: "Restore " + modelData.source; onClicked: { window.restoreIndex = index; window.restoreDestination = modelData.destination; restoreDialog.open() } }
                                Action { text: "Open backup"; flat: true; Accessible.name: "Open backup for " + modelData.source; onClicked: backend.openFolder(modelData.backup) }
                            }
                        }
                    }
                    PixelText { anchors.centerIn: parent; visible: historyList.count === 0; text: "Nothing added yet."; font.pixelSize: 26 }
                }
            }
        }
        ScrollView {
            id: settingsScroll; objectName: "settingsPage"
            clip: true; contentWidth: width
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AlwaysOff }
            ColumnLayout {
                width: settingsScroll.width; spacing: 10
                PixelText { text: "Settings"; Layout.margins: 16; Layout.bottomMargin: 0 }
                Hint { text: "psyche " + (Qt.application.version || ""); Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.topMargin: -4 }
                Hint {
                    visible: window.needsKey && !preferences.hasApiKey
                    text: "Search needs a Hubcap key."
                    Layout.leftMargin: 16; Layout.rightMargin: 16
                }
                Card {
                    Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                    ColumnLayout {
                        width: parent.width; spacing: 12
                        Label { textFormat: Text.PlainText; text: "Hubcap key"; color: window.ink }
                        Hint { visible: preferences.importedKey && !preferences.environmentKey; text: "Key from ACCELA." }
                        Hint { visible: preferences.environmentKey; text: "Environment key active." }
                        RowLayout {
                            Input {
                                id: apiKey; objectName: "apiKeyInput"; Layout.fillWidth: true
                                Component.onCompleted: text = preferences.apiKey
                                echoMode: reveal.checked ? TextInput.Normal : TextInput.Password
                                placeholderText: "Paste your key"; enabled: !backend.busy; Accessible.name: "Hubcap key"
                            }
                            Action { id: reveal; text: checked ? "Hide" : "Show"; checkable: true; Accessible.name: "Show Hubcap key" }
                        }
                        Tick { id: remember; text: "Remember on this computer"; checked: preferences.rememberKey; enabled: !backend.busy; Accessible.name: "Remember Hubcap key on this computer" }
                        Hint { visible: remember.checked; text: "Saved locally, unencrypted." }
                        RowLayout {
                            Action {
                                text: "Save"; primary: true; enabled: !backend.busy
                                onClicked: {
                                    if (preferences.savePreferences(apiKey.text, remember.checked, preferences.theme)) {
                                        savedLabel.text = "Saved."
                                        if (preferences.hasApiKey) window.needsKey = false
                                    }
                                }
                            }
                            Label { textFormat: Text.PlainText; id: savedLabel; color: window.muted }
                        }
                    }
                }
                Card {
                    Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                    ColumnLayout {
                        width: parent.width; spacing: 8
                        RowLayout {
                            Label { textFormat: Text.PlainText; text: "Hubcap"; color: window.ink; Layout.fillWidth: true }
                            Action { text: backend.checkingHubcap ? "Checking…" : "Check connection"; enabled: !backend.checkingHubcap; onClicked: backend.checkHubcap() }
                        }
                        Hint { visible: !!backend.hubcapInfo.health; text: "Service: " + (backend.hubcapInfo.health || "") }
                        Hint { visible: !!backend.hubcapInfo.healthError; text: backend.hubcapInfo.healthError || "" }
                        Hint { visible: !!backend.hubcapInfo.accountError; text: backend.hubcapInfo.accountError || "" }
                        Hint {
                            property var account: backend.hubcapInfo.account || ({})
                            visible: !!backend.hubcapInfo.account
                            text: (account.username || "Account") + " · Today: " + (account.daily_usage ?? "—") + " / " + (account.daily_limit ?? "—")
                                + (account.can_make_requests === false ? " · Blocked" : "")
                        }
                        Hint {
                            property var account: backend.hubcapInfo.account || ({})
                            visible: !!account.api_key_expires_at
                            text: "Expires: " + (account.api_key_expires_at || "")
                        }
                    }
                }
                Card {
                    Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                    ColumnLayout {
                        width: parent.width; spacing: 12
                        Label { textFormat: Text.PlainText; text: "SLSsteam directory"; color: window.ink }
                        PathField { text: preferences.destination || "Not found"; Accessible.name: "SLSsteam configuration directory" }
                        ChoiceBox {
                            visible: preferences.destinations.length > 1; Layout.fillWidth: true
                            model: preferences.destinations; textRole: "path"
                            currentIndex: window.pathIndex(preferences.destinations, preferences.destination)
                            displayText: currentIndex < 0 ? "Choose an installation…" : currentText
                            onActivated: preferences.chooseDestination(preferences.folderUrl(preferences.destinations[currentIndex].path))
                            Accessible.name: "Detected SLSsteam installations"
                        }
                        RowLayout {
                            Action { text: "Change directory"; enabled: !backend.busy; onClicked: destinationDialog.open() }
                            Action { text: "Detect"; enabled: !backend.busy; onClicked: preferences.detectPaths() }
                            Label { textFormat: Text.PlainText; text: preferences.destinationValid ? "Ready" : "Select a directory"; color: window.muted; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                    }
                }
                Item { Layout.preferredHeight: 16 }
            }
        }
    }
    footer: Pane {
        padding: 12; leftPadding: 24; rightPadding: 24
        background: Rectangle { color: window.canvas; Rectangle { width: parent.width; height: 1; color: window.border } }
        ColumnLayout {
            anchors.fill: parent; spacing: 8
            ProgressBar {
                Layout.fillWidth: true; visible: backend.busy; indeterminate: true
                background: Rectangle { implicitHeight: 4; color: window.raised; radius: 2 }
                contentItem: Item {
                    implicitHeight: 4
                    Rectangle { width: parent.width * 0.4; height: parent.height; radius: 2; color: window.accent }
                }
            }
            Label { textFormat: Text.PlainText;
                text: window.notice !== "" ? window.notice : preferences.settingsError ? preferences.message : backend.status
                color: window.notice !== "" || preferences.settingsError || backend.statusKind === "error" ? "#e2aaaa" : window.muted
                wrapMode: Text.WrapAnywhere; Layout.fillWidth: true; font.pixelSize: 12
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }
    Sheet {
        id: fetchDialog
        title: "Download from Hubcap?"
        contentItem: Label {
            textFormat: Text.PlainText; color: window.ink; wrapMode: Text.WordWrap
            Accessible.name: "Confirm Hubcap download"
            text: "This uses Hubcap daily quota.\n\nDownload " + window.contentLabel() + " for " + (window.pendingName || ("AppID " + window.pendingAppId)) + "?"
        }
        footer: DialogButtonBox {
            Action { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Action { text: "Download"; primary: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAccepted: backend.fetch(window.pendingAppId, window.pendingName)
    }
    Sheet {
        id: removeDialog
        title: "Remove " + window.removingName + "?"
        contentItem: Label {
            textFormat: Text.PlainText
            text: "Removes its AppIDs, depots and keys from config.yaml. This does not uninstall the game from Steam. Shared entries stay. A backup is saved."
                  + (preferences.steamRunning
                         ? "\n\nSteam is open. Restart Steam after removing or the library can stay out of date."
                         : "")
            color: window.ink; wrapMode: Text.WordWrap
            Accessible.name: text
        }
        footer: DialogButtonBox {
            Action { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Action { text: "Remove"; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAccepted: backend.removeGame(window.removingId)
    }
    Connections { target: tabs; function onCurrentIndexChanged() { if (tabs.currentIndex === window.tabLibrary) backend.refreshLibrary() } }
    Sheet {
        id: restoreDialog
        title: "Restore backup?"
        contentItem: Label {
            textFormat: Text.PlainText
            text: "This also undoes changes made after this import.\n\n" + window.restoreDestination
            wrapMode: Text.WrapAnywhere; color: window.ink
            Accessible.name: "Confirm restore backup"
        }
        footer: DialogButtonBox {
            Action { text: "Cancel"; DialogButtonBox.buttonRole: DialogButtonBox.RejectRole }
            Action { text: "Restore"; primary: true; DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole }
        }
        onAccepted: backend.restore(window.restoreIndex)
    }
    FileDialog { id: zipDialog; title: "Open ZIP"; currentFolder: preferences.importDirectory; nameFilters: ["ZIP packages (*.zip)"]; onAccepted: backend.inspect(selectedFile) }
    FolderDialog { id: destinationDialog; title: "SLSsteam directory"; currentFolder: preferences.folderUrl(preferences.destination); onAccepted: preferences.chooseDestination(selectedFolder) }
}
