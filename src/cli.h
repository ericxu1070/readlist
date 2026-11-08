#pragma once

#include <QStringList>

namespace Cli {
// True when argv names a command the CLI runs without a window.
bool isCommand(const QStringList &args);
// Runs it; returns the process exit code (0 ok, 1 error on stderr).
int run(const QStringList &args);
}
