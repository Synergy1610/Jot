#include "cloud_worker.h"

#include "../core/jot_core.h"

static QString takeCloudError(char *error) {
    if (!error) return QStringLiteral("Cloud request failed");
    const QString message = QString::fromUtf8(error);
    jot_core_string_free(error);
    return message;
}

void CloudWorker::initialize(const QString &url, const QString &key) {
    CloudResult result;
    result.kind = CloudResult::Initialize;
    char *error = nullptr;
    if (!jot_cloud_configure(url.toUtf8().constData(), key.toUtf8().constData(), &error)) {
        result.message = takeCloudError(error);
        emitResult(result);
        return;
    }
    if (!jot_cloud_restore(&error)) {
        result.message = takeCloudError(error);
        emitResult(result);
        return;
    }
    result.signedIn = jot_cloud_is_signed_in();
    if (!result.signedIn) {
        result.ok = true;
        result.message = QStringLiteral("Cloud backup is off");
        emitResult(result);
        return;
    }
    char *account = jot_cloud_account();
    if (account) {
        result.account = QString::fromUtf8(account);
        jot_core_string_free(account);
    }
    char *content = nullptr;
    char *updated = nullptr;
    qlonglong version = 0;
    if (jot_cloud_download(&content, &updated, &version, &error)) {
        result.ok = true;
        result.version = version;
        result.message = QStringLiteral("Backed up to the cloud");
        if (content) jot_core_string_free(content);
        if (updated) jot_core_string_free(updated);
    } else {
        result.message = takeCloudError(error);
    }
    emitResult(result);
}

void CloudWorker::signIn(const QString &email, const QString &password) {
    CloudResult result;
    result.kind = CloudResult::SignIn;
    char *error = nullptr;
    const QByteArray user = email.toUtf8();
    const QByteArray pass = password.toUtf8();
    if (!jot_cloud_sign_in(user.constData(), pass.constData(), &error)) {
        result.message = takeCloudError(error);
        emitResult(result);
        return;
    }
    char *account = jot_cloud_account();
    if (account) {
        result.account = QString::fromUtf8(account);
        jot_core_string_free(account);
    }
    result.ok = result.signedIn = jot_cloud_is_signed_in();
    result.message = result.ok ? QStringLiteral("Signed in") : QStringLiteral("Sign-in failed");
    emitResult(result);
}

void CloudWorker::signUp(const QString &email, const QString &password) {
    CloudResult result;
    result.kind = CloudResult::SignUp;
    char *error = nullptr;
    bool needsConfirmation = false;
    const QByteArray user = email.toUtf8();
    const QByteArray pass = password.toUtf8();
    if (!jot_cloud_sign_up(user.constData(), pass.constData(), &needsConfirmation, &error)) {
        result.message = takeCloudError(error);
        emitResult(result);
        return;
    }
    char *account = jot_cloud_account();
    if (account) {
        result.account = QString::fromUtf8(account);
        jot_core_string_free(account);
    }
    result.ok = true;
    result.signedIn = jot_cloud_is_signed_in();
    result.message = needsConfirmation
        ? QStringLiteral("Check your email to confirm, then sign in")
        : QStringLiteral("Signed in");
    emitResult(result);
}

void CloudWorker::signOut() {
    CloudResult result;
    result.kind = CloudResult::SignOut;
    char *error = nullptr;
    if (!jot_cloud_sign_out(&error)) {
        result.message = takeCloudError(error);
        emitResult(result);
        return;
    }
    result.ok = true;
    result.message = QStringLiteral("Signed out");
    emitResult(result);
}

void CloudWorker::sync(const QString &content, qlonglong version) {
    CloudResult result;
    result.kind = CloudResult::Sync;
    char *error = nullptr;
    qlonglong newVersion = 0;
    const QByteArray bytes = content.toUtf8();
    if (!jot_cloud_upload(bytes.constData(), version, &newVersion, &error)) {
        result.message = takeCloudError(error);
        emitResult(result);
        return;
    }
    result.ok = true;
    result.version = newVersion;
    result.message = QStringLiteral("Backed up to the cloud");
    emitResult(result);
}

void CloudWorker::pull() {
    CloudResult result;
    result.kind = CloudResult::Pull;
    char *error = nullptr;
    char *content = nullptr;
    char *updated = nullptr;
    qlonglong version = 0;
    if (!jot_cloud_download(&content, &updated, &version, &error)) {
        result.message = takeCloudError(error);
        emitResult(result);
        return;
    }
    result.ok = true;
    result.version = version;
    if (content) {
        result.content = QString::fromUtf8(content);
        jot_core_string_free(content);
    }
    if (updated) jot_core_string_free(updated);
    result.message = result.content.isEmpty()
        ? QStringLiteral("No cloud notes yet")
        : QStringLiteral("Downloaded from the cloud");
    emitResult(result);
}
