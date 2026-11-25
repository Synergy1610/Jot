#include "placement.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QWindow>
#include <cstdio>

Placement::Placement(QWindow *window, bool enabled, const QString &hyprctl,
                     const QString &className, QObject *parent)
    : QObject(parent), m_window(window), m_enabled(enabled), m_hyprctl(hyprctl),
      m_className(className) {
    m_retry.setSingleShot(true);
    m_timeout.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &Placement::startLookup);
    connect(&m_timeout, &QTimer::timeout, &m_process, &QProcess::kill);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &Placement::finished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_timeout.stop();
        m_requested = false;
        m_restartAfterExit = false;
        std::fprintf(stderr, "jot: could not start hyprctl; using normal window placement\n");
    });
    m_focusRetry.setSingleShot(true);
    m_focusTimeout.setSingleShot(true);
    connect(&m_focusRetry, &QTimer::timeout, this, [this] {
        if (!m_focusPending || !m_window->isVisible() || m_focusLua.isEmpty()) return;
        if (m_focusProcess.state() != QProcess::NotRunning) return;
        if (m_focusVerifying) {
            m_focusProcess.start(m_hyprctl, {QStringLiteral("-j"), QStringLiteral("activewindow")});
        } else {
            ++m_focusAttempts;
            m_focusProcess.start(m_hyprctl, {QStringLiteral("eval"), m_focusLua});
        }
        m_focusTimeout.start(1500);
    });
    connect(&m_focusTimeout, &QTimer::timeout, &m_focusProcess, &QProcess::kill);
    connect(&m_focusProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int exitCode, QProcess::ExitStatus status) {
        m_focusTimeout.stop();
        if (!m_focusPending || !m_window->isVisible()) return;
        if (m_focusVerifying) {
            m_focusVerifying = false;
            if (status == QProcess::NormalExit && exitCode == 0) {
                const auto active = QJsonDocument::fromJson(
                    m_focusProcess.readAllStandardOutput()).object();
                if (active.value(QStringLiteral("pid")).toInteger()
                    == QCoreApplication::applicationPid()) {
                    m_focusPending = false;
                    return;
                }
            }
            if (m_focusAttempts >= 12) {
                m_focusPending = false;
                return;
            }
        } else {
            m_focusVerifying = true;
        }
        m_focusRetry.start(m_focusVerifying ? 100 : 200);
    });
    connect(&m_focusProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_focusTimeout.stop();
        m_focusPending = false;
    });
    connect(m_window, &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible) summon();
        else cancel();
    });
}

void Placement::summon() {
    if (!m_enabled || !m_window->isVisible()) return;
    m_requested = true;
    m_attempts = 0;
    if (m_process.state() == QProcess::NotRunning) m_retry.start(0);
    else m_restartAfterExit = true;
}

void Placement::cancel() {
    m_requested = false;
    m_restartAfterExit = false;
    m_retry.stop();
    m_timeout.stop();
    m_focusRetry.stop();
    m_focusTimeout.stop();
    m_focusPending = false;
    m_focusVerifying = false;
    m_focusLua.clear();
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
    if (m_focusProcess.state() != QProcess::NotRunning) m_focusProcess.kill();
}

void Placement::startLookup() {
    if (!m_requested || !m_window->isVisible()) return;
    if (m_process.state() != QProcess::NotRunning) return;
    ++m_attempts;
    m_evaluating = false;
    m_focusPending = false;
    m_focusVerifying = false;
    m_focusLua.clear();
    m_process.start(m_hyprctl, {QStringLiteral("-j"), QStringLiteral("clients")});
    m_timeout.start(1500);
}

void Placement::finished(int exitCode, QProcess::ExitStatus status) {
    m_timeout.stop();
    const QByteArray output = m_process.readAllStandardOutput();
    const QByteArray error = m_process.readAllStandardError();
    if (m_restartAfterExit) {
        m_restartAfterExit = false;
        if (m_requested && m_window->isVisible()) m_retry.start(0);
        return;
    }
    if (!m_requested || !m_window->isVisible()) return;
    if (m_evaluating) {
        m_requested = false;
        if (status == QProcess::NormalExit && exitCode == 0 && output.trimmed() == "ok") {
            // The first dispatch happens at map time, before the compositor has
            // finished activating the window. Reapply focus briefly so it sticks.
            m_focusPending = true;
            m_focusVerifying = false;
            m_focusAttempts = 0;
            m_focusRetry.start(150);
        } else {
            std::fprintf(stderr, "jot: Hyprland placement failed: %s %s\n",
                         output.constData(), error.constData());
        }
        return;
    }
    bool mapped = false;
    bool fullscreen = false;
    bool floating = false;
    QSize compositorSize;
    QPoint compositorAt;
    if (status == QProcess::NormalExit && exitCode == 0) {
        const auto clients = QJsonDocument::fromJson(output).array();
        for (const QJsonValue &value : clients) {
            const QJsonObject client = value.toObject();
            if (client.value(QStringLiteral("pid")).toInteger() == QCoreApplication::applicationPid()
                && client.value(QStringLiteral("class")).toString() == m_className
                && client.value(QStringLiteral("mapped")).toBool()) {
                mapped = true;
                // Never change fullscreen state: it may belong to another app's
                // workspace and Hyprland may transfer it when this window maps.
                fullscreen = client.value(QStringLiteral("fullscreen")).toInt(-1) != 0;
                floating = client.value(QStringLiteral("floating")).toBool(false);
                const QJsonArray size = client.value(QStringLiteral("size")).toArray();
                if (size.size() == 2)
                    compositorSize = QSize(size.at(0).toInt(), size.at(1).toInt());
                const QJsonArray at = client.value(QStringLiteral("at")).toArray();
                if (at.size() == 2)
                    compositorAt = QPoint(at.at(0).toInt(), at.at(1).toInt());
                break;
            }
        }
    }
    if (mapped && fullscreen) {
        m_requested = false;
        std::fprintf(stderr, "jot: prototype is fullscreen; skipping experimental placement\n");
        return;
    }
    if (mapped && (!floating || compositorSize.width() <= 0 || compositorSize.height() <= 0)) {
        m_requested = false;
        std::fprintf(stderr, "jot: prototype is not safely floating; skipping experimental placement\n");
        return;
    }
    QScreen *screen = m_window->screen();
    if (!mapped || !screen) {
        if (m_attempts < 10) m_retry.start(100);
        else {
            m_requested = false;
            std::fprintf(stderr, "jot: prototype window was not mapped by Hyprland\n");
        }
        return;
    }
    const QRect area = screen->geometry();
    // Hyprland's size includes client decorations and can exceed Qt's
    // content size. Do not resize a fixed-size Wayland surface.
    const int x = area.x() + qMax(0, area.width() - compositorSize.width() - 24);
    const int y = area.y() + 24;
    // The only selector is the current process PID, never the installed Jot class.
    // Focus follows the pointer in the target desktop. Moving the cursor into
    // the pad is what makes focus stick; a focus dispatch alone is overridden
    // immediately by the pointer still resting on another window.
    const int centerX = x + qMax(1, compositorSize.width() / 2);
    const int centerY = y + qMax(1, compositorSize.height() / 2);
    QString lua = QStringLiteral(
        "local w=hl.get_window('pid:%1'); "
        "if w and w.pid==%1 and w.class=='%2' and w.mapped "
        "and not w.hidden and w.floating and w.fullscreen==0 then ")
        .arg(QCoreApplication::applicationPid()).arg(m_className);
    // A startup window rule may already have placed the pad. Avoid a second
    // move (and its visible jump) when the compositor reports the target spot.
    if (compositorAt != QPoint(x, y)) {
        lua += QStringLiteral(
            "hl.dispatch(hl.dsp.window.move({x=%1,y=%2,relative=false,window=w})); ")
            .arg(x).arg(y);
    }
    lua += QStringLiteral(
        "hl.dispatch(hl.dsp.window.alter_zorder({mode='top',window=w})); "
        "hl.dispatch(hl.dsp.focus({window=w})); "
        "hl.dispatch(hl.dsp.cursor.move({x=%1,y=%2})) end")
        .arg(centerX).arg(centerY);
    m_focusLua = lua;
    m_evaluating = true;
    m_process.start(m_hyprctl, {QStringLiteral("eval"), lua});
    m_timeout.start(1500);
}
