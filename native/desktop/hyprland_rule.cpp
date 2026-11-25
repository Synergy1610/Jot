#include "hyprland_rule.h"

#include <QByteArray>
#include <QProcess>

bool installJotPlacementRule(const QString &hyprctl, QString *error) {
    const QString lua = QStringLiteral(
        "hl.window_rule({ name='jot-placement', match={class='^jot$'}, "
        "float=true, move='(monitor_w-window_w-24) 24' })");

    QProcess process;
    process.start(hyprctl, {QStringLiteral("eval"), lua});
    if (!process.waitForStarted(1500) || !process.waitForFinished(3000)) {
        process.kill();
        process.waitForFinished(1000);
        if (error) *error = QStringLiteral("could not run hyprctl for the placement rule");
        return false;
    }
    const QByteArray output = process.readAllStandardOutput().trimmed();
    const QByteArray message = process.readAllStandardError().trimmed();
    if (process.exitCode() != 0 || output != "ok") {
        if (error) {
            *error = message.isEmpty()
                ? QString::fromUtf8(output)
                : QString::fromUtf8(message);
        }
        return false;
    }
    return true;
}
