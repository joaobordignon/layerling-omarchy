import QtQuick
import QtWebEngine

// One layerling page: the start page or an editor. Every window uses the same
// profile, so they all see the same designs.
WebEngineView {
    id: view
    profile: webProfile

    // An editor page, as opposed to the start page.
    readonly property bool isEditor: url.toString().indexOf("editor=") >= 0

    Component.onCompleted: {
        // Without Chrome's file system access API the page falls back to
        // downloads and file fields, which go through the portal.
        let script = WebEngine.script()
        script.name = "layerling-file-fallback"
        script.injectionPoint = WebEngineScript.DocumentCreation
        script.worldId = WebEngineScript.MainWorld
        script.runsOnSubFrames = true
        script.sourceCode = "for (const name of ['showOpenFilePicker', 'showSaveFilePicker', 'showDirectoryPicker']) {"
                + " try { Object.defineProperty(window, name, { value: undefined, configurable: true }) } catch (e) {} }"
        userScripts.insert(script)
    }

    onFileDialogRequested: function(request) {
        request.accepted = true
        files.pickForPage(request, request.mode === FileDialogRequest.FileModeOpenMultiple)
    }

    // Links out of the app (the guide, GitHub, a printer's website) open in
    // the user's browser instead of replacing the editor.
    onNewWindowRequested: function(request) { Qt.openUrlExternally(request.requestedUrl) }
    onNavigationRequested: function(request) {
        if (request.isMainFrame && !request.url.toString().startsWith(backend.url)) {
            request.reject()
            Qt.openUrlExternally(request.url)
        }
    }

    onPermissionRequested: function(permission) {
        if (permission.permissionType === WebEnginePermission.PermissionType.ClipboardReadWrite)
            permission.grant()
        else
            permission.deny()
    }

    onRenderProcessTerminated: function(status, code) {
        if (status !== WebEngineView.NormalTerminationStatus)
            reloadAfterCrash.start()
    }
    Timer { id: reloadAfterCrash; interval: 500; onTriggered: view.reload() }
}
