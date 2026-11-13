#pragma once

#include <QString>

// Where layerling's pieces live. The server files are installed read-only, so
// everything that is written (browser storage, preview pictures, the bridge
// token, exports in progress) goes under the user's data directory.
namespace Paths {

// The server always listens here. Designs live in the web view's storage,
// which belongs to the page's origin including its port, so a different port
// would show an empty start page.
constexpr int port = 47317;
QString baseUrl();

// The built server: $LAYERLING_RUNTIME, then build/runtime beside a binary
// run from the project, then the installed copy in /usr/lib/layerling.
QString runtimeDirectory();

// ~/.local/share/layerling, or $LAYERLING_DATA when tests point it elsewhere.
QString dataDirectory();

// Node from $LAYERLING_NODE, the system package, or the PATH; empty if none.
QString nodeExecutable();

// The secret that lets an MCP client drive the editor, made on first use and
// readable only by the user.
QString mcpToken(const QString &dataDirectory);

// Builds a tree of links into the read-only runtime with writable folders
// where the server writes, and returns the server.js to start inside it.
// Next's standalone server changes into its own folder and keeps preview
// pictures under ./.codex there, so that folder must be the user's.
QString prepareServerTree(const QString &runtime, const QString &dataDirectory, QString *error);

}
