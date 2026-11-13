import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Layerling

ApplicationWindow {
    id: win
    width: 1400
    height: 900
    minimumWidth: 800
    minimumHeight: 560
    visible: true
    title: view.title || "layerling"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    // The page asked for before the server was ready, if any.
    property string pendingUrl: instance.initialUrl

    // Shows `url` (or the start page): in this window while it holds no
    // editor, otherwise in a window of its own.
    function open(url) {
        if (backend.state !== Backend.Ready) {
            if (url !== "")
                pendingUrl = url
            win.show()
            win.requestActivate()
            return
        }
        const target = url !== "" ? url : backend.url
        if (!win.visible || !view.isEditor) {
            view.url = target
            win.show()
            win.requestActivate()
        } else {
            editorWindow.createObject(null, { startUrl: target })
        }
    }

    Connections {
        target: instance
        function onOpenRequested(url) { win.open(url) }
    }
    Connections {
        target: backend
        function onStateChanged() {
            if (backend.state === Backend.Ready && view.url.toString() === "") {
                view.url = win.pendingUrl !== "" ? win.pendingUrl : backend.url
                win.pendingUrl = ""
            }
        }
    }

    // Only Ctrl chords: every other key belongs to the editor, and these are
    // ones layerling leaves to the browser and never binds itself.
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut {
        sequence: "Ctrl+R"
        context: Qt.WindowShortcut
        onActivated: backend.state === Backend.Ready ? view.reload() : backend.start()
    }
    Shortcut { sequences: ["Ctrl+=", "Ctrl++"]; context: Qt.WindowShortcut; onActivated: view.zoomFactor = Math.min(3, view.zoomFactor + 0.1) }
    Shortcut { sequence: "Ctrl+-"; context: Qt.WindowShortcut; onActivated: view.zoomFactor = Math.max(0.5, view.zoomFactor - 0.1) }
    Shortcut { sequence: "Ctrl+0"; context: Qt.WindowShortcut; onActivated: view.zoomFactor = 1 }

    Component {
        id: editorWindow
        ApplicationWindow {
            id: extra
            property url startUrl
            width: win.width
            height: win.height
            visible: true
            title: page.title || "layerling"
            color: "#0e0e10"
            Material.theme: Material.Dark
            Material.accent: theme.accent
            Material.roundedScale: theme.radius
            onClosing: extra.destroy()

            Shortcut { sequence: "Ctrl+W"; context: Qt.WindowShortcut; onActivated: extra.close() }
            Shortcut { sequence: "Ctrl+R"; context: Qt.WindowShortcut; onActivated: page.reload() }
            Shortcut { sequences: ["Ctrl+=", "Ctrl++"]; context: Qt.WindowShortcut; onActivated: page.zoomFactor = Math.min(3, page.zoomFactor + 0.1) }
            Shortcut { sequence: "Ctrl+-"; context: Qt.WindowShortcut; onActivated: page.zoomFactor = Math.max(0.5, page.zoomFactor - 0.1) }
            Shortcut { sequence: "Ctrl+0"; context: Qt.WindowShortcut; onActivated: page.zoomFactor = 1 }

            EditorPage { id: page; anchors.fill: parent; url: extra.startUrl }
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        EditorPage {
            id: view
            anchors.fill: parent
            visible: backend.state === Backend.Ready
        }

        ColumnLayout {
            anchors.centerIn: parent
            visible: backend.state === Backend.Starting
            spacing: 16

            BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: parent.visible }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: qsTr("Starting layerling…")
                color: "#c8c8cc"
                font.pixelSize: 16
            }
        }

        ColumnLayout {
            anchors.centerIn: parent
            width: Math.min(parent.width - 64, 720)
            visible: backend.state === Backend.Failed
            spacing: 14

            Label {
                text: qsTr("layerling could not start")
                font.pixelSize: 22
                font.bold: true
                color: "white"
            }
            Label {
                Layout.fillWidth: true
                text: backend.error
                wrapMode: Text.Wrap
                color: "#c8c8cc"
                font.pixelSize: 15
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 220
                visible: backend.log !== ""
                color: "#16161a"
                radius: theme.radius
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 10
                    TextArea {
                        readOnly: true
                        text: backend.log
                        font.family: "monospace"
                        font.pixelSize: 12
                        color: "#a0a0a8"
                        wrapMode: TextEdit.Wrap
                        background: null
                    }
                }
            }
            Button {
                text: qsTr("Try again")
                highlighted: true
                Material.foreground: theme.accentForeground
                onClicked: backend.start()
            }
        }

        // What happened to an export or an import, briefly, at the bottom.
        Rectangle {
            id: toast
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 28
            width: Math.min(toastText.implicitWidth + 36, parent.width - 48)
            height: toastText.implicitHeight + 20
            radius: theme.radius
            color: "#1d1d22"
            border.color: theme.accent
            opacity: 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 180 } }

            Label {
                id: toastText
                anchors.centerIn: parent
                width: Math.min(implicitWidth, toast.parent.width - 84)
                text: files.notice
                wrapMode: Text.Wrap
                color: "white"
            }
            Timer { id: toastTimer; interval: 4500; onTriggered: toast.opacity = 0 }
            Connections {
                target: files
                function onNoticeChanged() { toast.opacity = 1; toastTimer.restart() }
            }
        }
    }
}
