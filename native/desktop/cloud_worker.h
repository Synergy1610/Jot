#pragma once

#include <QObject>
#include <QString>

struct CloudResult {
    enum Kind { Initialize, SignIn, SignUp, SignOut, Sync, Pull };
    int kind = Initialize;
    bool ok = false;
    QString message;
    QString account;
    bool signedIn = false;
    qlonglong version = 0;
    QString content;
};
Q_DECLARE_METATYPE(CloudResult)

// Runs Supabase calls on a dedicated thread so the UI thread never blocks.
class CloudWorker : public QObject {
    Q_OBJECT
public slots:
    void initialize(const QString &url, const QString &key);
    void signIn(const QString &email, const QString &password);
    void signUp(const QString &email, const QString &password);
    void signOut();
    void sync(const QString &content, qlonglong version);
    void pull();

signals:
    void finished(const CloudResult &result);

private:
    void emitResult(const CloudResult &result) { emit finished(result); }
};
