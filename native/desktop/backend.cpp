#include "backend.h"

#include <QByteArray>
#include <QClipboard>
#include <QDate>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace {
class DateTagHighlighter : public QSyntaxHighlighter {
public:
    explicit DateTagHighlighter(QTextDocument *document) : QSyntaxHighlighter(document) {}

protected:
    void highlightBlock(const QString &text) override {
        static const QRegularExpression tag(
            QStringLiteral(R"(\s*[|·]\s+\d{1,2}\s+[A-Za-z]{3}\s+\d{4}\s*$)"));
        const auto match = tag.match(text);
        if (match.hasMatch()) {
            QTextCharFormat format;
            format.setForeground(QColor(QStringLiteral("#87a9b0")));
            setFormat(match.capturedStart(), match.capturedLength(), format);
        }
    }
};
}

QString Backend::takeError(char *error) {
    if (!error) return tr("Unknown storage error");
    const QString message = QString::fromUtf8(error);
    jot_core_string_free(error);
    return message;
}

Backend::Backend(const QString &databasePath, QObject *parent)
    : QObject(parent), m_databasePath(databasePath),
      m_hotkey(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/hypr") {
    char *error = nullptr;
    const QByteArray path = databasePath.toUtf8();
    m_pad = jot_core_open(path.constData(), &error);
    if (!m_pad) {
        setStatus(takeError(error));
        return;
    }
    char *value = jot_core_load(m_pad, &error);
    if (!value) {
        setStatus(takeError(error));
        return;
    }
    m_saved = m_content = QString::fromUtf8(value);
    jot_core_string_free(value);
    m_loaded = true;
    m_timer.setSingleShot(true);
    m_timer.setInterval(300);
    connect(&m_timer, &QTimer::timeout, this, &Backend::flush);
    setStatus(tr("Saved on this device"));
    refreshUndo();
    QTimer::singleShot(0, this, &Backend::loadCloudConfig);
}

QString Backend::displayDate() const {
    return QLocale::c().toString(QDate::currentDate(), QStringLiteral("ddd dd MMM yyyy"));
}

Backend::~Backend() {
    jot_core_close(m_pad);
}

void Backend::setTrayAvailable(bool available) {
    if (m_trayAvailable == available) return;
    m_trayAvailable = available;
    emit trayAvailableChanged();
}

void Backend::setStatus(const QString &status) {
    m_status = status;
    emit statusChanged();
}

bool Backend::copyOriginal() {
    if (!incompatibleText() || !QGuiApplication::clipboard()) return false;
    QGuiApplication::clipboard()->setText(m_saved);
    return true;
}

bool Backend::backupTo(const QString &path) {
    if (!m_pad) return false;
    char *error = nullptr;
    const QByteArray destination = path.toUtf8();
    if (!jot_core_backup(m_pad, destination.constData(), &error)) {
        setStatus(takeError(error));
        return false;
    }
    return true;
}

bool Backend::migrateLegacy() {
    if (!m_pad) return false;
    char *error = nullptr;
    if (!jot_core_migrate(m_pad, &error)) {
        setStatus(takeError(error));
        return false;
    }
    // The stored text may have changed; keep the editor and the save baseline
    // in sync so migration never looks like an unsaved user edit.
    char *value = jot_core_load(m_pad, &error);
    if (!value) {
        setStatus(takeError(error));
        return false;
    }
    const QString migrated = QString::fromUtf8(value);
    jot_core_string_free(value);
    if (migrated != m_content) {
        m_content = m_saved = migrated;
        if (m_document) replaceDocument(migrated);
        emit contentChanged();
    }
    return true;
}

void Backend::runCloudTask(const std::function<void()> &task) {
    if (m_cloudSaving) return;
    m_cloudSaving = true;
    m_cloudSaved = false;
    emit cloudChanged();
    // Let QML paint the spinner before a blocking network call.
    QTimer::singleShot(0, this, [this, task] {
        task();
        m_cloudSaving = false;
        emit cloudChanged();
    });
}

void Backend::finishCloud(bool ok, const QString &message) {
    m_cloudStatus = message;
    m_cloudSaved = ok && m_cloudSignedIn;
    emit cloudChanged();
}

void Backend::refreshCloudAccount() {
    char *account = jot_cloud_account();
    m_cloudAccount = account ? QString::fromUtf8(account) : QString();
    if (account) jot_core_string_free(account);
    m_cloudSignedIn = jot_cloud_is_signed_in();
    emit cloudChanged();
}

void Backend::loadCloudConfig() {
    const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/jot/supabase.env");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_cloudStatus = tr("Cloud backup is off");
        emit cloudChanged();
        return;
    }
    QString url, key;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const int split = line.indexOf(QLatin1Char('='));
        if (split <= 0 || line.startsWith(QLatin1Char('#'))) continue;
        const QString name = line.left(split).trimmed();
        const QString value = line.mid(split + 1).trimmed();
        if (name == QStringLiteral("SUPABASE_URL")) url = value;
        if (name == QStringLiteral("SUPABASE_ANON_KEY")) key = value;
    }
    file.close();
    char *error = nullptr;
    if (url.isEmpty() || key.isEmpty()
        || !jot_cloud_configure(url.toUtf8().constData(), key.toUtf8().constData(), &error)) {
        m_cloudStatus = error ? takeError(error) : tr("Cloud configuration is incomplete");
        emit cloudChanged();
        return;
    }
    if (!jot_cloud_restore(&error)) {
        m_cloudStatus = error ? takeError(error) : tr("Cloud backup is off");
        emit cloudChanged();
        return;
    }
    refreshCloudAccount();
    if (!m_cloudSignedIn) {
        m_cloudStatus = tr("Cloud backup is off");
        return;
    }
    char *content = nullptr;
    char *updated = nullptr;
    qlonglong version = 0;
    if (jot_cloud_download(&content, &updated, &version, &error)) {
        m_cloudVersion = version;
        m_cloudSaved = true;
        m_cloudStatus = tr("Backed up to the cloud");
        if (content) jot_core_string_free(content);
        if (updated) jot_core_string_free(updated);
    } else {
        m_cloudStatus = takeError(error);
    }
    emit cloudChanged();
}

void Backend::cloudSignIn(const QString &email, const QString &password) {
    const QString user = email, pass = password;
    runCloudTask([this, user, pass] {
        char *error = nullptr;
        const QByteArray emailBytes = user.toUtf8();
        const QByteArray passwordBytes = pass.toUtf8();
        if (!jot_cloud_sign_in(emailBytes.constData(), passwordBytes.constData(), &error)) {
            finishCloud(false, takeError(error));
            return;
        }
        refreshCloudAccount();
        finishCloud(true, tr("Signed in"));
    });
}

void Backend::cloudSignUp(const QString &email, const QString &password) {
    const QString user = email, pass = password;
    runCloudTask([this, user, pass] {
        char *error = nullptr;
        bool needsConfirmation = false;
        const QByteArray emailBytes = user.toUtf8();
        const QByteArray passwordBytes = pass.toUtf8();
        if (!jot_cloud_sign_up(emailBytes.constData(), passwordBytes.constData(),
                               &needsConfirmation, &error)) {
            finishCloud(false, takeError(error));
            return;
        }
        refreshCloudAccount();
        finishCloud(true, needsConfirmation
            ? tr("Check your email to confirm, then sign in")
            : tr("Signed in"));
    });
}

void Backend::cloudSignOut() {
    runCloudTask([this] {
        char *error = nullptr;
        if (!jot_cloud_sign_out(&error)) {
            finishCloud(false, takeError(error));
            return;
        }
        m_cloudVersion = 0;
        refreshCloudAccount();
        finishCloud(true, tr("Signed out"));
    });
}

void Backend::cloudSyncNow() {
    if (!m_cloudSignedIn) {
        finishCloud(false, tr("Sign in before backing up to the cloud"));
        return;
    }
    const QString content = m_content;
    runCloudTask([this, content] {
        char *error = nullptr;
        qlonglong version = 0;
        const QByteArray bytes = content.toUtf8();
        if (!jot_cloud_upload(bytes.constData(), m_cloudVersion, &version, &error)) {
            finishCloud(false, takeError(error));
            return;
        }
        m_cloudVersion = version;
        finishCloud(true, tr("Backed up to the cloud"));
    });
}

void Backend::cloudPull() {
    if (!m_cloudSignedIn) {
        finishCloud(false, tr("Sign in before downloading cloud notes"));
        return;
    }
    runCloudTask([this] {
        char *error = nullptr;
        char *content = nullptr;
        char *updated = nullptr;
        qlonglong version = 0;
        if (!jot_cloud_download(&content, &updated, &version, &error)) {
            finishCloud(false, takeError(error));
            return;
        }
        if (!content) {
            finishCloud(true, tr("No cloud notes yet"));
            return;
        }
        const QString text = QString::fromUtf8(content);
        jot_core_string_free(content);
        if (updated) jot_core_string_free(updated);
        QTextDocument probe;
        probe.setPlainText(text);
        QString represented;
        for (QTextBlock block = probe.begin(); block.isValid(); block = block.next()) {
            if (block != probe.begin()) represented += QLatin1Char('\n');
            represented += block.text();
        }
        if (represented != text) {
            finishCloud(false, tr("The cloud notes cannot be represented by this editor"));
            return;
        }
        const QByteArray bytes = text.toUtf8();
        if (!jot_core_save(m_pad, m_saved.toUtf8().constData(), bytes.constData(), &error)) {
            finishCloud(false, takeError(error));
            return;
        }
        m_timer.stop();
        m_saved = m_content = text;
        if (m_document) replaceDocument(text);
        emit contentChanged();
        m_cloudVersion = version;
        finishCloud(true, tr("Downloaded from the cloud"));
    });
}

void Backend::edit(const QString &text) {
    if (!ready() || text == m_content) return;
    m_content = text;
    emit undoChanged();
    if (m_cloudSaved) {
        m_cloudSaved = false;
        emit cloudChanged();
    }
    if (m_content == m_saved) {
        m_timer.stop();
        setStatus(tr("Saved on this device"));
    } else {
        // Start first: the status change is what notifies QML bindings that
        // depend on `saving`, so the spinner sees the active timer.
        m_timer.start();
        setStatus(tr("Saving on this device…"));
    }
}

void Backend::attachEditor(QQuickTextDocument *document) {
    if (!document || !document->textDocument()) return;
    m_document = document->textDocument();
    if (ready() && documentText() != m_content) {
        m_loaded = false;
        setStatus(tr("This editor cannot represent these notes without changing them. Editing is disabled."));
        return;
    }
    connect(m_document, &QTextDocument::contentsChanged, this, &Backend::editDocument);
    new DateTagHighlighter(m_document);
}

QString Backend::documentText() const {
    if (!m_document) return {};
    // QTextEdit's plain-text accessor converts non-breaking spaces to spaces.
    // Reading the blocks preserves those characters while restoring newlines.
    QString result;
    for (QTextBlock block = m_document->begin(); block.isValid(); block = block.next()) {
        if (block != m_document->begin()) result += QLatin1Char('\n');
        result += block.text();
    }
    return result;
}

void Backend::refreshUndo() {
    if (!m_pad) return;
    char *error = nullptr;
    char *backup = jot_core_last_cleared(m_pad, &error);
    m_hasBackup = backup != nullptr;
    if (backup) jot_core_string_free(backup);
    if (error) setStatus(takeError(error));
    emit undoChanged();
}

void Backend::replaceDocument(const QString &text) {
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    cursor.insertText(text);
    cursor.endEditBlock();
}

bool Backend::clearPad() {
    if (!ready() || !m_document || m_content.isEmpty()) return false;
    if (m_content.contains(QChar::Null)) {
        setStatus(tr("Cannot save text containing a NUL character"));
        return false;
    }
    if (!m_content.isValidUtf16()) {
        setStatus(tr("Cannot save text containing an unpaired UTF-16 surrogate"));
        return false;
    }
    char *error = nullptr;
    const QByteArray saved = m_saved.toUtf8();
    const QByteArray current = m_content.toUtf8();
    if (!jot_core_clear(m_pad, saved.constData(), current.constData(), &error)) {
        setStatus(takeError(error));
        return false;
    }
    m_timer.stop();
    m_saved.clear();
    replaceDocument(QString());
    editDocument();
    refreshUndo();
    setStatus(tr("Saved on this device"));
    return true;
}

bool Backend::undoClear() {
    if (!canUndoClear() || !m_document) return false;
    char *error = nullptr;
    char *value = jot_core_last_cleared(m_pad, &error);
    if (!value) {
        if (error) setStatus(takeError(error));
        else refreshUndo();
        return false;
    }
    const QString backup = QString::fromUtf8(value);
    jot_core_string_free(value);
    // Check representability before changing either the database or editor.
    QTextDocument probe;
    probe.setPlainText(backup);
    QString represented;
    for (QTextBlock block = probe.begin(); block.isValid(); block = block.next()) {
        if (block != probe.begin()) represented += QLatin1Char('\n');
        represented += block.text();
    }
    if (represented != backup) {
        setStatus(tr("The undo copy cannot be represented safely by this editor."));
        return false;
    }
    const QByteArray bytes = backup.toUtf8();
    if (!jot_core_undo_clear(m_pad, bytes.constData(), &error)) {
        setStatus(takeError(error));
        return false;
    }
    m_timer.stop();
    m_saved = backup;
    replaceDocument(backup);
    editDocument();
    refreshUndo();
    setStatus(tr("Saved on this device"));
    return true;
}

void Backend::editDocument() {
    if (m_document) edit(documentText());
}

int Backend::finishLine(int selectionStart, int selectionEnd) {
    const QString text = documentText();
    if (!ready() || !m_document || selectionStart < 0 || selectionEnd < selectionStart
        || selectionEnd > text.size() || m_document->characterCount() != text.size() + 1)
        return -1;

    const int lineStart = selectionStart == 0 ? 0 : text.lastIndexOf('\n', selectionStart - 1) + 1;
    int lineEnd = text.indexOf('\n', selectionEnd);
    if (lineEnd < 0) lineEnd = text.size();
    const QString line = text.mid(lineStart, lineEnd - lineStart);
    static const QRegularExpression tag(
        QStringLiteral(R"(\s*[|·]\s+\d{1,2}\s+[A-Za-z]{3}\s+\d{4}\s*$)"),
        QRegularExpression::UseUnicodePropertiesOption);

    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    cursor.setPosition(lineEnd);
    if (!tag.match(line).hasMatch()) {
        int trimmedEnd = lineEnd;
        while (trimmedEnd > lineStart
               && (text.at(trimmedEnd - 1).isSpace() || text.at(trimmedEnd - 1) == QChar(0xfeff)))
            --trimmedEnd;
        cursor.setPosition(trimmedEnd, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        const QDate today = QDate::currentDate();
        cursor.insertText(QStringLiteral("  | %1 %2 %3")
            .arg(today.day())
            .arg(QLocale::c().monthName(today.month(), QLocale::ShortFormat))
            .arg(today.year()));
    }
    cursor.insertBlock();
    const int caret = cursor.position();
    cursor.endEditBlock();
    return caret;
}

bool Backend::flush() {
    m_timer.stop();
    if (!ready()) return false;
    if (m_content == m_saved) return true;
    // The C interface is NUL-terminated; reject embedded NUL instead of losing data.
    if (m_content.contains(QChar::Null)) {
        setStatus(tr("Cannot save text containing a NUL character"));
        return false;
    }
    if (!m_content.isValidUtf16()) {
        setStatus(tr("Cannot save text containing an unpaired UTF-16 surrogate"));
        return false;
    }
    char *error = nullptr;
    const QByteArray expected = m_saved.toUtf8();
    const QByteArray content = m_content.toUtf8();
    if (!jot_core_save(m_pad, expected.constData(), content.constData(), &error)) {
        setStatus(takeError(error));
        return false;
    }
    m_saved = m_content;
    setStatus(tr("Saved on this device"));
    if (m_cloudSignedIn) QTimer::singleShot(0, this, &Backend::cloudSyncNow);
    return true;
}
