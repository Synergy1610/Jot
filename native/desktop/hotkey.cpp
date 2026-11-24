#include "hotkey.h"

#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <QUrl>

Hotkey::Hotkey(const QString &directory, QObject *parent)
    : QObject(parent), m_directory(directory), m_status(tr("Not checked")) {}

QString Hotkey::snippet() const {
    return QStringLiteral("o.bind(\"SUPER + W\", \"Jot scratchpad\", \"jot toggle\")");
}

QString Hotkey::configPath() const {
    return QDir(m_directory).filePath(QStringLiteral("bindings.lua"));
}

void Hotkey::setStatus(const QString &status) {
    m_status = status;
    emit statusChanged();
}

void Hotkey::refresh() {
    QDirIterator files(m_directory, {QStringLiteral("*.lua"), QStringLiteral("*.conf")},
                       QDir::Files, QDirIterator::Subdirectories);
    const QRegularExpression command(QStringLiteral(R"(\bjot\s+toggle\b)"),
                                     QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression luaBinding(
        QStringLiteral(R"(\bo\.bind\s*\(\s*["']\s*super\s*\+\s*w\s*["'])"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression confBinding(
        QStringLiteral(R"(\bbind\w*\s*=\s*super\s*,\s*w\s*,)"),
        QRegularExpression::CaseInsensitiveOption);
    bool unreadable = false;
    while (files.hasNext()) {
        QFile file(files.next());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            unreadable = true;
            continue;
        }
        bool blockComment = false;
        const bool lua = QFileInfo(file).suffix().compare(QStringLiteral("lua"), Qt::CaseInsensitive) == 0;
        while (!file.atEnd()) {
            QString line = QString::fromUtf8(file.readLine());
            if (lua && blockComment) {
                const int end = line.indexOf(QStringLiteral("]]"));
                if (end < 0) continue;
                line.remove(0, end + 2);
                blockComment = false;
            }
            if (lua) {
                const int comment = line.indexOf(QStringLiteral("--"));
                if (comment >= 0) {
                    if (line.mid(comment, 4) == QStringLiteral("--[[")
                        && line.indexOf(QStringLiteral("]]"), comment + 4) < 0)
                        blockComment = true;
                    line.truncate(comment);
                }
            } else {
                const int comment = line.indexOf(QLatin1Char('#'));
                if (comment >= 0) line.truncate(comment);
            }
            if (command.match(line).hasMatch()
                && ((lua && luaBinding.match(line).hasMatch())
                    || (!lua && confBinding.match(line).hasMatch()))) {
                setStatus(tr("Looks configured"));
                return;
            }
        }
    }
    setStatus(unreadable ? tr("Could not read all Hyprland config files") : tr("Not found"));
}

bool Hotkey::copy() {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        setStatus(tr("Clipboard unavailable"));
        return false;
    }
    clipboard->setText(snippet());
    return true;
}

bool Hotkey::openConfig() {
    if (!QFile::exists(configPath()) || !QDesktopServices::openUrl(QUrl::fromLocalFile(configPath()))) {
        setStatus(tr("Could not open the Hyprland bindings file"));
        return false;
    }
    return true;
}
