#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>

class QWindow;

// Addresses only this process's prototype window; never installs compositor rules.
class Placement : public QObject {
    Q_OBJECT
public:
    explicit Placement(QWindow *window, bool enabled,
                       const QString &hyprctl = QStringLiteral("hyprctl"),
                       const QString &className = QStringLiteral("jot-native-prototype"),
                       QObject *parent = nullptr);
public slots:
    void summon();

private:
    void startLookup();
    void finished(int exitCode, QProcess::ExitStatus status);
    void cancel();
    QWindow *m_window;
    bool m_enabled;
    QString m_hyprctl;
    QString m_className;
    QProcess m_process;
    QProcess m_focusProcess;
    QTimer m_retry;
    QTimer m_timeout;
    QTimer m_focusRetry;
    QTimer m_focusTimeout;
    QString m_focusLua;
    int m_attempts = 0;
    int m_focusAttempts = 0;
    bool m_requested = false;
    bool m_evaluating = false;
    bool m_restartAfterExit = false;
    bool m_focusPending = false;
    bool m_focusVerifying = false;
};
