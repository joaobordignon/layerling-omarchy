// Runs in every layerling page of the app, after qwebchannel.js. It turns the
// app's OrcaBridge (web channel object `orca`) into window.layerlingDesktop,
// promises in and out, and tells the page with a "layerling-desktop" event.
// The patched editor (OrcaDesktopPanel.tsx) shows its OrcaSlicer panel when
// this is there; in a browser it never is.
(function () {
    "use strict";
    if (window.top !== window || typeof QWebChannel !== "function")
        return;

    function connect() {
        if (!window.qt || !window.qt.webChannelTransport)
            return false;
        new QWebChannel(window.qt.webChannelTransport, function (channel) {
            var orca = channel.objects.orca;
            if (!orca)
                return;
            var pending = {};
            // Every window's page shares the one bridge: ids start somewhere random
            // so two pages never wait on the same answer.
            var next = Math.floor(Math.random() * 1e9);
            orca.finished.connect(function (id, json) {
                var done = pending[id];
                if (!done)
                    return;
                delete pending[id];
                try {
                    done(JSON.parse(json));
                } catch (error) {
                    done({ ok: false, error: String(error) });
                }
            });
            function call(method, args) {
                return new Promise(function (resolve) {
                    var id = ++next;
                    pending[id] = resolve;
                    orca[method].apply(orca, [id].concat(args));
                });
            }
            window.layerlingDesktop = Object.freeze({
                version: 1,
                slicer: "OrcaSlicer",
                presets: function () { return call("presets", []); },
                slice: function (request) { return call("slice", [JSON.stringify(request)]); },
                open: function (request) { return call("open", [JSON.stringify(request)]); },
            });
            window.dispatchEvent(new Event("layerling-desktop"));
        });
        return true;
    }

    if (!connect())
        document.addEventListener("DOMContentLoaded", connect, { once: true });
})();
