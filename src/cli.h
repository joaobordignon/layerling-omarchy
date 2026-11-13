#pragma once

#include <QString>
#include <QStringList>

// Commands that need no window. `layerling` and `layerling open` start the
// app; the others let an agent drive it.
namespace Cli {

// Returns the exit code of a command, or -1 when the arguments ask for the
// window, so main() goes on to start the GUI; `openUrl` then holds the page
// to show, if `layerling open <url>` named one.
int run(int argc, char *argv[], QString *openUrl);

// The bundled SKILL.md with its developer setup steps replaced by the app's.
QString adaptSkill(const QString &skill);

}
