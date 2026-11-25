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

    qRegisterMetaType<CloudResult>("CloudResult");
    m_cloudThread = new QThread(this);
    m_cloudWorker = new CloudWorker;
    m_cloudWorker->moveToThread(m_cloudThread);
    connect(m_cloudWorker, &CloudWorker::finished, this, &Backend::onCloudFinished);
    connect(m_cloudThread, &QThread::finished, m_cloudWorker, &QObject::deleteLater);
    m_cloudThread->start();
    m_cloudSyncTimer.setSingleShot(true);
    m_cloudSyncTimer.setInterval(1500);
    connect(&m_cloudSyncTimer, &QTimer::timeout, this, &Backend::startCloudSync);
    QTimer::singleShot(0, this, &Backend::loadCloudConfig);
}

QString Backend::displayDate() const {
    return QLocale::c().toString(QDate::currentDate(), QStringLiteral("ddd dd MMM yyyy"));
}

Backend::~Backend() {
    if (m_cloudThread) {
        m_cloudThread->quit();
        m_cloudThread->wait(3000);
    }
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

void Backend::finishCloud(bool ok, const QString &message) {
    m_cloudStatus = message;
    m_cloudSaved = ok && m_cloudSignedIn;
    emit cloudChanged();
}

void Backend::beginCloudSaving() {
    m_cloudSaving = true;
    m_cloudSavingElapsed.start();
    emit cloudChanged();
}

void Backend::afterCloudSavingEnds() {
    emit cloudChanged();
    if (m_cloudSyncPending) {
        m_cloudSyncPending = false;
        if (m_cloudSignedIn && !m_cloudSaving) m_cloudSyncTimer.start();
    }
}

void Backend::endCloudSaving() {
    const qint64 elapsed = m_cloudSavingElapsed.elapsed();
    const qint64 minimum = 600;
    if (elapsed >= minimum) {
        m_cloudSaving = false;
        afterCloudSavingEnds();
        return;
    }
    QTimer::singleShot(static_cast<int>(minimum - elapsed), this, [this] {
        m_cloudSaving = false;
        afterCloudSavingEnds();
    });
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
    if (url.isEmpty() || key.isEmpty()) {
        m_cloudStatus = tr("Cloud configuration is incomplete");
        emit cloudChanged();
        return;
    }
    beginCloudSaving();
    m_cloudRequestContent = m_content;
    QMetaObject::invokeMethod(m_cloudWorker, "initialize", Qt::QueuedConnection,
                              Q_ARG(QString, url), Q_ARG(QString, key),
                              Q_ARG(QString, m_cloudRequestContent));
}

void Backend::scheduleCloudSync() {
    if (!m_cloudSignedIn) return;
    if (m_cloudSaving) {
        m_cloudSyncPending = true;
        return;
    }
    m_cloudSyncTimer.start();
}

void Backend::startCloudSync() {
    if (!m_cloudSignedIn) return;
    if (m_cloudSaving) {
        m_cloudSyncPending = true;
        return;
    }
    beginCloudSaving();
    m_cloudSaved = false;
    emit cloudChanged();
    m_cloudRequestContent = m_content;
    QMetaObject::invokeMethod(m_cloudWorker, "sync", Qt::QueuedConnection,
                              Q_ARG(QString, m_cloudRequestContent), Q_ARG(qlonglong, m_cloudVersion));
}

void Backend::onCloudFinished(const CloudResult &result) {
    switch (result.kind) {
    case CloudResult::Initialize:
        m_cloudAccount = result.account;
        m_cloudSignedIn = result.signedIn;
        m_cloudVersion = result.version;
        m_cloudStatus = result.ok
            ? (result.signedIn ? result.message : tr("Cloud backup is off"))
            : result.message;
        m_cloudSaved = result.ok && result.signedIn && result.inSync
            && m_content == m_cloudRequestContent;
        if (result.ok && result.signedIn && !result.inSync) {
            m_cloudSaved = false;
            m_cloudSyncPending = true;
        }
        break;
    case CloudResult::SignIn:
    case CloudResult::SignUp:
        m_cloudAccount = result.account;
        m_cloudSignedIn = result.signedIn;
        m_cloudSaved = false;
        m_cloudStatus = result.message;
        if (result.ok && result.signedIn) m_cloudSyncPending = true;
        break;
    case CloudResult::SignOut:
        m_cloudAccount.clear();
        m_cloudSignedIn = false;
        m_cloudVersion = 0;
        m_cloudSaved = false;
        m_cloudRequestContent.clear();
        m_cloudStatus = result.message;
        break;
    case CloudResult::Sync: {
        if (!result.ok) {
            m_cloudSaved = false;
            m_cloudStatus = result.message;
            break;
        }
        m_cloudVersion = result.version;
        if (m_content != m_cloudRequestContent) {
            // The user kept typing while this upload was in flight. Applying
            // the merged snapshot would delete that newer text, so leave the
            // editor alone and let the next sync carry it.
            m_cloudSaved = false;
            m_cloudStatus = tr("Cloud updated while you were typing — syncing again…");
            m_cloudSyncPending = true;
            break;
        }
        const bool applied = result.content == m_content || applyCloudContent(result.content);
        m_cloudSaved = applied;
        if (applied) m_cloudStatus = result.message;
        break;
    }
    case CloudResult::Pull: {
        if (!result.ok) {
            m_cloudSaved = false;
            m_cloudStatus = result.message;
            break;
        }
        if (m_content != m_cloudRequestContent) {
            m_cloudSaved = false;
            m_cloudStatus = tr("Notes changed while downloading — nothing was replaced");
            break;
        }
        const bool applied = applyCloudContent(result.content);
        m_cloudVersion = applied ? result.version : m_cloudVersion;
        m_cloudSaved = applied;
        if (applied) m_cloudStatus = result.message;
        break;
    }
    }
    endCloudSaving();
}

void Backend::cloudSignIn(const QString &email, const QString &password) {
    if (m_cloudSaving) return;
    beginCloudSaving();
    QMetaObject::invokeMethod(m_cloudWorker, "signIn", Qt::QueuedConnection,
                              Q_ARG(QString, email), Q_ARG(QString, password));
}

void Backend::cloudSignUp(const QString &email, const QString &password) {
    if (m_cloudSaving) return;
    beginCloudSaving();
    QMetaObject::invokeMethod(m_cloudWorker, "signUp", Qt::QueuedConnection,
                              Q_ARG(QString, email), Q_ARG(QString, password));
}

void Backend::cloudSignOut() {
    if (m_cloudSaving) return;
    beginCloudSaving();
    QMetaObject::invokeMethod(m_cloudWorker, "signOut", Qt::QueuedConnection);
}

void Backend::cloudSyncNow() {
    startCloudSync();
}

void Backend::cloudPull() {
    if (!m_cloudSignedIn) {
        finishCloud(false, tr("Sign in before downloading cloud notes"));
        return;
    }
    if (m_cloudSaving) return;
    beginCloudSaving();
    m_cloudRequestContent = m_content;
    QMetaObject::invokeMethod(m_cloudWorker, "pull", Qt::QueuedConnection);
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

// Replace the pad with cloud content without destroying edits the user just
// typed: a merge only adds a prefix above the local text, so insert that
// prefix instead of rewriting the whole document.
bool Backend::applyCloudContent(const QString &text) {
    if (text == m_content) return true;
    QTextDocument probe;
    probe.setPlainText(text);
    QString represented;
    for (QTextBlock block = probe.begin(); block.isValid(); block = block.next()) {
        if (block != probe.begin()) represented += QLatin1Char('\n');
        represented += block.text();
    }
    if (represented != text) {
        m_cloudStatus = tr("The cloud notes cannot be represented by this editor");
        return false;
    }
    char *error = nullptr;
    if (!jot_core_save(m_pad, m_saved.toUtf8().constData(), text.toUtf8().constData(), &error)) {
        m_cloudStatus = takeError(error);
        return false;
    }
    m_timer.stop();
    const QString previous = m_content;
    m_saved = m_content = text;
    if (m_document) {
        if (text.endsWith(previous)) {
            const QString prefix = text.left(text.size() - previous.size());
            QTextCursor cursor(m_document);
            cursor.beginEditBlock();
            cursor.setPosition(0);
            cursor.insertText(prefix);
            cursor.endEditBlock();
        } else {
            replaceDocument(text);
        }
    }
    emit contentChanged();
    return true;
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
    scheduleCloudSync();
    return true;
}
