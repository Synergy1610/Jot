#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QThread>
#include <functional>

#include "cloud_worker.h"
#include <QPointer>
#include <QString>
#include <QTimer>
#include "../core/jot_core.h"
#include "hotkey.h"

class QQuickTextDocument;
class QTextDocument;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString content READ content NOTIFY contentChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(bool canUndoClear READ canUndoClear NOTIFY undoChanged)
    Q_PROPERTY(bool hasContent READ hasContent NOTIFY undoChanged)
    Q_PROPERTY(QString databasePath READ databasePath CONSTANT)
    Q_PROPERTY(bool startHidden READ startHidden CONSTANT)
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged)
    Q_PROPERTY(Hotkey *hotkey READ hotkey CONSTANT)
    Q_PROPERTY(bool locallySaved READ locallySaved NOTIFY statusChanged)
    Q_PROPERTY(bool incompatibleText READ incompatibleText NOTIFY statusChanged)
    Q_PROPERTY(bool saving READ saving NOTIFY statusChanged)
    Q_PROPERTY(bool cloudSaving READ cloudSaving NOTIFY cloudChanged)
    Q_PROPERTY(bool cloudSaved READ cloudSaved NOTIFY cloudChanged)
    Q_PROPERTY(bool cloudSignedIn READ cloudSignedIn NOTIFY cloudChanged)
    Q_PROPERTY(QString cloudAccount READ cloudAccount NOTIFY cloudChanged)
    Q_PROPERTY(QString cloudStatus READ cloudStatus NOTIFY cloudChanged)
    Q_PROPERTY(QString cloudTooltip READ cloudTooltip NOTIFY cloudChanged)
    Q_PROPERTY(bool localError READ localError NOTIFY statusChanged)
    Q_PROPERTY(QString localTooltip READ localTooltip NOTIFY statusChanged)
    Q_PROPERTY(QString appTitle READ appTitle CONSTANT)
public:
    explicit Backend(const QString &databasePath, QObject *parent = nullptr);
    ~Backend() override;
    QString content() const { return m_content; }
    QString status() const { return m_status; }
    bool ready() const { return m_pad != nullptr && m_loaded; }
    bool canUndoClear() const { return ready() && m_hasBackup && m_content.isEmpty(); }
    bool hasContent() const { return !m_content.isEmpty(); }
    QString databasePath() const { return m_databasePath; }
    bool startHidden() const { return m_startHidden; }
    void setStartHidden(bool hidden) { m_startHidden = hidden; }
    bool trayAvailable() const { return m_trayAvailable; }
    Hotkey *hotkey() { return &m_hotkey; }
    bool locallySaved() const { return ready() && m_content == m_saved && m_status == tr("Saved on this device"); }
    bool incompatibleText() const { return m_pad && !m_loaded; }
    bool saving() const { return m_timer.isActive(); }
    bool cloudSaving() const { return m_cloudSaving; }
    bool cloudSaved() const { return m_cloudSaved; }
    bool cloudSignedIn() const { return m_cloudSignedIn; }
    Q_INVOKABLE bool isCloudSignedIn() const { return m_cloudSignedIn; }
    QString cloudAccount() const { return m_cloudAccount; }
    QString cloudStatus() const { return m_cloudStatus; }
    QString cloudTooltip() const {
        if (!m_cloudSignedIn) return tr("Cloud backup is off — sign in to enable");
        return tr("Cloud backup is on — syncs when you minimise or close Jot");
    }
    bool localError() const { return m_loaded && m_content != m_saved && !m_timer.isActive(); }
    QString localTooltip() const {
        if (!m_pad || !m_loaded) return tr("Local save unavailable");
        if (m_content != m_saved)
            return m_timer.isActive() ? tr("Saving on this device…") : tr("Local save failed");
        return tr("Saved on this device");
    }
    Q_INVOKABLE QString displayDate() const;
#ifdef JOT_PRODUCTION
    QString appTitle() const { return QStringLiteral("Jot"); }
#else
    QString appTitle() const { return QStringLiteral("Jot native prototype"); }
#endif
    void setTrayAvailable(bool available);
    Q_INVOKABLE void edit(const QString &text);
    Q_INVOKABLE bool flush();
    Q_INVOKABLE void attachEditor(QQuickTextDocument *document);
    Q_INVOKABLE void editDocument();
    Q_INVOKABLE int finishLine(int selectionStart, int selectionEnd);
    Q_INVOKABLE bool clearPad();
    Q_INVOKABLE bool undoClear();
    Q_INVOKABLE bool copyOriginal();
    Q_INVOKABLE void cloudSignIn(const QString &email, const QString &password);
    Q_INVOKABLE void cloudSignUp(const QString &email, const QString &password);
    Q_INVOKABLE void cloudSignOut();
    Q_INVOKABLE void cloudSyncNow();
    Q_INVOKABLE void cloudPull();
    Q_INVOKABLE void syncAfterHide();
    Q_INVOKABLE void quitAfterSync();
    bool backupTo(const QString &path);
    bool migrateLegacy();

signals:
    void contentChanged();
    void statusChanged();
    void undoChanged();
    void trayAvailableChanged();
    void cloudChanged();
    void cloudTaskFinished();
    void readyToQuit();

private:
    void setStatus(const QString &status);
    QString documentText() const;
    void refreshUndo();
    void replaceDocument(const QString &text);
    bool applyCloudContent(const QString &text);
    void loadCloudConfig();
    void refreshCloudAccount();
    void finishCloud(bool ok, const QString &message);
    void beginCloudSaving();
    void endCloudSaving();
    void afterCloudSavingEnds();
    void requestCloudSync();
    void maybeFinishQuit();
    void startCloudSync();
    void onCloudFinished(const CloudResult &result);
    static QString takeError(char *error);
    JotCorePad *m_pad = nullptr;
    QString m_content;
    QString m_saved;
    QString m_status;
    QTimer m_timer;
    bool m_loaded = false;
    bool m_hasBackup = false;
    QString m_databasePath;
    bool m_startHidden = false;
    bool m_trayAvailable = false;
    bool m_cloudSaving = false;
    bool m_cloudTaskActive = false;
    bool m_cloudSaved = false;
    bool m_cloudSignedIn = false;
    qlonglong m_cloudVersion = 0;
    QString m_cloudAccount;
    QString m_cloudStatus;
    QThread *m_cloudThread = nullptr;
    CloudWorker *m_cloudWorker = nullptr;
    QTimer m_quitTimer;
    QString m_cloudRequestContent;
    QElapsedTimer m_cloudSavingElapsed;
    bool m_cloudSyncPending = false;
    bool m_quitPending = false;
    bool m_quitReadyEmitted = false;
    QPointer<QTextDocument> m_document;
    Hotkey m_hotkey;
};
