#include "../desktop/backend.h"
#include "../desktop/hyprland_rule.h"
#include "../desktop/placement.h"

#include <QFileInfo>
#include <QClipboard>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QScreen>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QtTest>

class NativeTests : public QObject {
    Q_OBJECT
private slots:
    void editorSavesAndReopens();
    void concurrentEditDoesNotOverwrite();
    void dateTagsRemainEditableAndUndoable();
    void closeKeepsConflictingEdits();
    void editorPreservesUnusualText();
    void incompatibleTextIsNotSaved();
    void invalidUtf16IsNotSavedOrCleared();
    void settingsClearAndUndo();
    void clearConflictPreservesPendingText();
    void undoConflictPreservesBothCopies();
    void hideAndQuitRefuseUnsavedConflicts();
    void hiddenSaveFailureShowsEditor();
    void escapeSavesAndHides();
    void hotkeyHelpUsesDisposableConfig();
    void savingBadgeStates();
    void startupRuleInstallsAndReportsFailure();
    void placementTargetsOnlyPrototypeAndCancelsWhenHidden();
    void fixedWindowHintsProtectFullscreenOnMap();
};

static bool createDatabase(const QString &path) {
    const QString name = QStringLiteral("create");
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            ok = query.exec("CREATE TABLE pad (id INTEGER PRIMARY KEY, content TEXT NOT NULL, updated_at TEXT, dirty INTEGER NOT NULL DEFAULT 0, synced_at TEXT)")
                 && query.exec("CREATE TABLE settings (key TEXT PRIMARY KEY, value TEXT)")
                 && query.exec("INSERT INTO pad (id, content) VALUES (1, 'test note')");
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

void NativeTests::editorSavesAndReopens() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    {
        Backend backend(path);
        QVERIFY(backend.ready());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        auto *editor = window->findChild<QQuickItem *>("padEditor");
        QVERIFY(editor);
        editor->forceActiveFocus();
        QTRY_VERIFY(editor->hasActiveFocus());
        QTest::keyClick(window, Qt::Key_End, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Space);
        QTest::keyClick(window, Qt::Key_A);
        QTest::keyClick(window, Qt::Key_D);
        QTest::keyClick(window, Qt::Key_D);
        QTest::keyClick(window, Qt::Key_E);
        QTest::keyClick(window, Qt::Key_D);
        QTRY_COMPARE(backend.content(), QString("test note added"));
        QTRY_COMPARE(backend.status(), QString("Saved on this device"));
        const QByteArray screenshot = qgetenv("JOT_NATIVE_TEST_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(QString::fromUtf8(screenshot)));
        }
    }
    Backend reopened(path);
    QVERIFY(reopened.ready());
    QCOMPARE(reopened.content(), QString("test note added"));
}

void NativeTests::concurrentEditDoesNotOverwrite() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    Backend first(path);
    Backend second(path);
    first.edit("first edit");
    QVERIFY(first.flush());
    second.edit("unsaved second edit");
    QVERIFY(!second.flush());
    QVERIFY(second.status().contains("changed in another process"));
    QCOMPARE(second.content(), QString("unsaved second edit"));
    Backend check(path);
    QCOMPARE(check.content(), QString("first edit"));
}

void NativeTests::dateTagsRemainEditableAndUndoable() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    Backend backend(path);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *editor = window->findChild<QQuickItem *>("padEditor");
    QVERIFY(editor);
    editor->forceActiveFocus();
    QTRY_VERIFY(editor->hasActiveFocus());
    const QDate today = QDate::currentDate();
    const QString tag = QStringLiteral("  | %1 %2 %3").arg(today.day())
        .arg(QLocale::c().monthName(today.month(), QLocale::ShortFormat)).arg(today.year());

    // Enter in the middle finishes the whole line without losing its suffix.
    editor->setProperty("text", QString::fromUtf8("hello 🖋 world  \nnext"));
    editor->setProperty("cursorPosition", 3);
    QTest::keyClick(window, Qt::Key_Return);
    const QString finished = QString::fromUtf8("hello 🖋 world") + tag + "\n\nnext";
    QTRY_COMPARE(backend.content(), finished);
    QCOMPARE(editor->property("cursorPosition").toInt(), finished.indexOf('\n') + 1);

    auto *quickDocument = qvariant_cast<QQuickTextDocument *>(editor->property("textDocument"));
    QVERIFY(quickDocument);
    auto *document = quickDocument->textDocument();
    QVERIFY(document);
    QTRY_VERIFY(!document->firstBlock().layout()->formats().isEmpty());
    QCOMPARE(document->firstBlock().layout()->formats().first().format.foreground().color(),
             QColor("#87a9b0"));
    const QByteArray screenshot = qgetenv("JOT_NATIVE_TAG_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(QString::fromUtf8(screenshot)));
    }

    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE(backend.content(), QString::fromUtf8("hello 🖋 world  \nnext"));
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_COMPARE(backend.content(), finished);

    // Existing tags stay editable and are not duplicated.
    editor->setProperty("text", "old  | 1 Jan 2020");
    editor->setProperty("cursorPosition", 2);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(backend.content(), QString("old  | 1 Jan 2020\n"));
    editor->setProperty("cursorPosition", 0);
    QTest::keyClick(window, Qt::Key_X);
    QTRY_COMPARE(backend.content(), QString("xold  | 1 Jan 2020\n"));
    QTest::keyClick(window, Qt::Key_Z, Qt::ControlModifier);
    QTRY_COMPARE(backend.content(), QString("old  | 1 Jan 2020\n"));
    // Older dot separators remain editable and are not silently rewritten.
    const QString legacy = QString::fromUtf8("legacy  · 1 Jan 2020");
    editor->setProperty("text", legacy);
    editor->setProperty("cursorPosition", 3);
    QTest::keyClick(window, Qt::Key_Return);
    QTRY_COMPARE(backend.content(), legacy + "\n");
    QTRY_VERIFY(!document->firstBlock().layout()->formats().isEmpty());
    QCOMPARE(document->firstBlock().layout()->formats().first().format.foreground().color(),
             QColor("#87a9b0"));
    QVERIFY(backend.flush());
    Backend reopened(path);
    QCOMPARE(reopened.content(), legacy + "\n");
}

void NativeTests::closeKeepsConflictingEdits() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    Backend backend(path);
    Backend other(path);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *editor = window->findChild<QQuickItem *>("padEditor");
    QVERIFY(editor);
    editor->setProperty("text", "unsaved local text");
    other.edit("other process text");
    QVERIFY(other.flush());
    QVERIFY(!window->close());
    QVERIFY(window->isVisible());
    QCOMPARE(backend.content(), QString("unsaved local text"));
    QVERIFY(backend.status().contains("changed in another process"));
}

void NativeTests::editorPreservesUnusualText() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    const QString original = QString::fromUtf8("literal <b>html</b>  🖋\nnonbreaking space\n");
    {
        Backend writer(path);
        writer.edit(original);
        QVERIFY(writer.flush());
    }
    Backend backend(path);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *editor = window->findChild<QQuickItem *>("padEditor");
    QVERIFY(editor);
    auto *quickDocument = qvariant_cast<QQuickTextDocument *>(editor->property("textDocument"));
    QVERIFY(quickDocument);
    QVERIFY(backend.ready());
    editor->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_End, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_X);
    QTRY_COMPARE(backend.content(), original + "x");
    QVERIFY(backend.flush());
    Backend reopened(path);
    QCOMPARE(reopened.content(), original + "x");
    const int spaceIndex = original.indexOf(QChar(0x00a0));
    QVERIFY(spaceIndex >= 0);
    editor->setProperty("cursorPosition", spaceIndex);
    QTest::keyClick(window, Qt::Key_Delete);
    QTest::keyClick(window, Qt::Key_Space);
    QString replaced = original + "x";
    replaced[spaceIndex] = QLatin1Char(' ');
    QTRY_COMPARE(backend.content(), replaced);
}

void NativeTests::incompatibleTextIsNotSaved() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    const QString original = QStringLiteral("carriage\rreturn");
    {
        Backend writer(path);
        writer.edit(original);
        QVERIFY(writer.flush());
    }
    Backend backend(path);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *editor = engine.rootObjects().first()->findChild<QQuickItem *>("padEditor");
    QVERIFY(editor);
    QVERIFY(!backend.ready());
    QVERIFY(!editor->isEnabled());
    QVERIFY(backend.status().contains("Editing is disabled"));
    auto *copy = engine.rootObjects().first()->findChild<QObject *>("copyOriginalButton");
    QVERIFY(copy);
    QVERIFY(copy->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(copy, "click"));
    QCOMPARE(QGuiApplication::clipboard()->text(), original);
    Backend reopened(path);
    QCOMPARE(reopened.content(), original);
}

void NativeTests::invalidUtf16IsNotSavedOrCleared() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    Backend backend(path);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    QString invalid = QStringLiteral("pending");
    invalid.append(QChar(0xd800));
    backend.edit(invalid);
    QVERIFY(!backend.flush());
    QVERIFY(backend.status().contains("unpaired"));
    QVERIFY(!backend.clearPad());
    QCOMPARE(backend.content(), invalid);
    Backend reopened(path);
    QCOMPARE(reopened.content(), QString("test note"));
    QVERIFY(!reopened.canUndoClear());
}

void NativeTests::settingsClearAndUndo() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    const QString original = QString::fromUtf8("pending notes 🖋\n");
    {
        Backend backend(path);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QObject *root = engine.rootObjects().first();
        auto *editor = root->findChild<QQuickItem *>("padEditor");
        auto *open = root->findChild<QObject *>("openSettings");
        auto *clear = root->findChild<QObject *>("clearButton");
        auto *cancel = root->findChild<QObject *>("cancelClearButton");
        auto *confirm = root->findChild<QObject *>("confirmClearButton");
        auto *popup = root->findChild<QObject *>("settingsPopup");
        QVERIFY(editor && open && clear && cancel && confirm && popup);
        QCOMPARE(popup->property("width").toInt(), 480);
        QCOMPARE(popup->property("height").toInt(), 320);
        auto *title = root->findChild<QQuickItem *>("settingsTitle");
        auto *body = root->findChild<QQuickItem *>("settingsBody");
        auto *copySnippet = root->findChild<QQuickItem *>("copySnippetButton");
        QVERIFY(title && body && copySnippet);
        const QFont titleFont = qvariant_cast<QFont>(title->property("font"));
        const QFont bodyFont = qvariant_cast<QFont>(body->property("font"));
        QCOMPARE(titleFont.family(), QString("monospace"));
        QCOMPARE(titleFont.pixelSize(), 17);
        auto *close = root->findChild<QQuickItem *>("settingsClose");
        QVERIFY(close);
        QCOMPARE(close->property("implicitWidth").toInt(), 30);
        QCOMPARE(bodyFont.family(), QString("monospace"));
        QCOMPARE(bodyFont.pixelSize(), 13);
        QCOMPARE(body->property("color").value<QColor>(), QColor("#FAFCFB"));
        auto *heading = root->findChild<QQuickItem *>("settingsAccountHeading");
        QVERIFY(heading);
        const QFont headingFont = qvariant_cast<QFont>(heading->property("font"));
        QCOMPARE(headingFont.pixelSize(), 15);
        QCOMPARE(heading->property("color").value<QColor>(), QColor("#87a9b0"));
        QCOMPARE(copySnippet->property("leftPadding").toInt(), 8);
        QCOMPARE(copySnippet->property("rightPadding").toInt(), 8);
        QCOMPARE(copySnippet->property("implicitHeight").toInt(), 34);
        editor->setProperty("text", original);
        QCOMPARE(backend.content(), original);
        QVERIFY(QMetaObject::invokeMethod(open, "click"));
        const QByteArray screenshot = qgetenv("JOT_NATIVE_SETTINGS_SCREENSHOT");
        if (!screenshot.isEmpty()) {
            QTest::qWait(100);
            auto *window = qobject_cast<QQuickWindow *>(root);
            QVERIFY(window);
            QVERIFY(window->grabWindow().save(QString::fromUtf8(screenshot)));
        }
        const QByteArray bottomShot = qgetenv("JOT_NATIVE_SETTINGS_BOTTOM_SCREENSHOT");
        if (!bottomShot.isEmpty()) {
            auto *flick = root->findChild<QQuickItem *>("settingsFlick");
            auto *window = qobject_cast<QQuickWindow *>(root);
            QVERIFY(flick && window);
            flick->setProperty("contentY", flick->property("contentHeight").toReal()
                               - flick->property("height").toReal());
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(QString::fromUtf8(bottomShot)));
        }
        QVERIFY(QMetaObject::invokeMethod(clear, "click"));
        QVERIFY(popup->property("confirmClear").toBool());
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        QVERIFY(QMetaObject::invokeMethod(open, "click"));
        QVERIFY(!popup->property("confirmClear").toBool());
        QVERIFY(QMetaObject::invokeMethod(clear, "click"));
        QVERIFY(QMetaObject::invokeMethod(cancel, "click"));
        QCOMPARE(backend.content(), original);
        QVERIFY(QMetaObject::invokeMethod(clear, "click"));
        QVERIFY(QMetaObject::invokeMethod(confirm, "click"));
        QCOMPARE(backend.content(), QString());
        QVERIFY(backend.canUndoClear());
    }
    {
        Backend backend(path);
        QVERIFY(backend.canUndoClear());
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("backend", &backend);
        engine.load(QUrl("qrc:/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QObject *root = engine.rootObjects().first();
        auto *open = root->findChild<QObject *>("openSettings");
        auto *undo = root->findChild<QObject *>("undoClearButton");
        QVERIFY(open && undo);
        QVERIFY(QMetaObject::invokeMethod(open, "click"));
        QVERIFY(QMetaObject::invokeMethod(undo, "click"));
        QCOMPARE(backend.content(), original);
        QVERIFY(!backend.canUndoClear());
    }
    Backend reopened(path);
    QCOMPARE(reopened.content(), original);
    QVERIFY(!reopened.canUndoClear());
}

void NativeTests::clearConflictPreservesPendingText() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    Backend backend(path);
    Backend other(path);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *editor = engine.rootObjects().first()->findChild<QQuickItem *>("padEditor");
    QVERIFY(editor);
    editor->setProperty("text", "pending local text");
    other.edit("other process text");
    QVERIFY(other.flush());
    QVERIFY(!backend.clearPad());
    QCOMPARE(backend.content(), QString("pending local text"));
    QCOMPARE(editor->property("text").toString(), QString("pending local text"));
    QVERIFY(backend.status().contains("changed in another process"));
    Backend reopened(path);
    QCOMPARE(reopened.content(), QString("other process text"));
}

void NativeTests::undoConflictPreservesBothCopies() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    Backend backend(path);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    QVERIFY(backend.clearPad());
    QVERIFY(backend.canUndoClear());
    Backend other(path);
    other.edit("newer notes");
    QVERIFY(other.flush());
    QVERIFY(!backend.undoClear());
    QVERIFY(backend.canUndoClear());
    QCOMPARE(backend.content(), QString());
    QVERIFY(backend.status().contains("changed"));
    Backend reopened(path);
    QCOMPARE(reopened.content(), QString("newer notes"));
    char *error = nullptr;
    const QByteArray pathBytes = path.toUtf8();
    JotCorePad *core = jot_core_open(pathBytes.constData(), &error);
    QVERIFY(core);
    char *backup = jot_core_last_cleared(core, &error);
    QVERIFY(backup);
    QCOMPARE(QString::fromUtf8(backup), QString("test note"));
    jot_core_string_free(backup);
    jot_core_close(core);
}

void NativeTests::hideAndQuitRefuseUnsavedConflicts() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    Backend backend(path);
    Backend other(path);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *editor = window->findChild<QQuickItem *>("padEditor");
    QVERIFY(editor);
    editor->setProperty("text", "pending local text");
    other.edit("other process text");
    QVERIFY(other.flush());
    for (const QString &command : {QStringLiteral("hide"), QStringLiteral("quit")}) {
        QVariant result;
        QVERIFY(QMetaObject::invokeMethod(window, "handleCommand", Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, QVariant(command))));
        QVERIFY(result.toString().startsWith("error"));
        QVERIFY(window->isVisible());
        QCOMPARE(backend.content(), QString("pending local text"));
    }
}

void NativeTests::hiddenSaveFailureShowsEditor() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    Backend backend(path);
    Backend other(path);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *editor = window->findChild<QQuickItem *>("padEditor");
    QVERIFY(editor);
    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(window, "handleCommand", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, QVariant("hide"))));
    QVERIFY(!window->isVisible());
    editor->setProperty("text", "pending local text");
    other.edit("other process text");
    QVERIFY(other.flush());
    QVERIFY(QMetaObject::invokeMethod(window, "handleCommand", Q_RETURN_ARG(QVariant, result),
                                      Q_ARG(QVariant, QVariant("quit"))));
    QVERIFY(result.toString().startsWith("error"));
    QVERIFY(window->isVisible());
    QCOMPARE(backend.content(), QString("pending local text"));
}

void NativeTests::escapeSavesAndHides() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    Backend backend(path);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *editor = window->findChild<QQuickItem *>("padEditor");
    QVERIFY(editor);
    editor->forceActiveFocus();
    QTRY_VERIFY(editor->hasActiveFocus());
    QTest::keyClick(window, Qt::Key_End, Qt::ControlModifier);
    QTest::keyClick(window, Qt::Key_X);
    QTRY_COMPARE(backend.content(), QString("test notex"));
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->isVisible());
    Backend reopened(path);
    QCOMPARE(reopened.content(), QString("test notex"));
}

void NativeTests::hotkeyHelpUsesDisposableConfig() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath("hypr/nested")));
    QFile file(dir.filePath("hypr/nested/bindings.lua"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write("-- o.bind(\"SUPER + W\", \"Jot\", \"jot toggle\")\n"
               "--[[\no.bind(\"SUPER + W\", \"Jot\", \"jot toggle\")\n]]\n"
               "o.bind(\"SUPER + X\", \"Jot\", \"jot toggle\")\n");
    file.close();
    Hotkey helper(dir.filePath("hypr"));
    helper.refresh();
    QCOMPARE(helper.status(), QString("Not found"));
    QVERIFY(file.open(QIODevice::Append | QIODevice::Text));
    file.write("o.bind(\"SUPER + W\", \"Jot scratchpad\", \"jot toggle\")\n");
    file.close();
    helper.refresh();
    QCOMPARE(helper.status(), QString("Looks configured"));
    QCOMPARE(helper.configPath(), dir.filePath("hypr/bindings.lua"));
    QVERIFY(helper.copy());
    QCOMPARE(QGuiApplication::clipboard()->text(),
             QString("o.bind(\"SUPER + W\", \"Jot scratchpad\", \"jot toggle\")"));
}

void NativeTests::savingBadgeStates() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    Backend backend(path);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    auto *localSpinner = window->findChild<QObject *>("localSpinner");
    auto *cloudSpinner = window->findChild<QObject *>("cloudSpinner");
    auto *tooltip = window->findChild<QObject *>("badgeTooltip");
    QVERIFY(localSpinner && cloudSpinner && tooltip);
    auto *localBadge = window->findChild<QQuickItem *>("localBadge");
    auto *cloudBadgeItem = window->findChild<QQuickItem *>("cloudBadge");
    auto *monitor = window->findChild<QQuickItem *>("monitorIcon");
    auto *cloudIconItem = window->findChild<QQuickItem *>("cloudIcon");
    auto *localTickItem = window->findChild<QQuickItem *>("localTick");
    auto *cloudTickItem = window->findChild<QQuickItem *>("cloudTick");
    auto *editor = window->findChild<QQuickItem *>("padEditor");
    auto *gear = window->findChild<QQuickItem *>("gearIcon");
    QVERIFY(editor && gear && localBadge && cloudBadgeItem && monitor && cloudIconItem
            && localTickItem && cloudTickItem);
    QCOMPARE(gear->width(), 17);
    QCOMPARE(gear->height(), 17);
    QCOMPARE(localBadge->width(), 36);
    QCOMPARE(cloudBadgeItem->width(), 48);
    QCOMPARE(cloudBadgeItem->x(), localBadge->x() + localBadge->width() + 4);
    QCOMPARE(localTickItem->x(), monitor->x() + monitor->width() + 3);
    QCOMPARE(cloudTickItem->x(), cloudIconItem->x() + cloudIconItem->width() + 18);

    QVERIFY(backend.locallySaved());
    QVERIFY(!backend.saving());
    QCOMPARE(backend.localTooltip(), QString("Saved on this device"));
    QVERIFY(!localSpinner->property("visible").toBool());
    QVERIFY(!cloudSpinner->property("visible").toBool());

    editor->setProperty("text", "a new note");
    QTRY_VERIFY(backend.saving());
    QVERIFY(backend.saving());
    QVERIFY(!backend.locallySaved());
    QCOMPARE(backend.localTooltip(), QString("Saving on this device…"));
    QVERIFY(localSpinner->property("visible").toBool());
    QVERIFY(!cloudSpinner->property("visible").toBool());

    const QByteArray screenshot = qgetenv("JOT_NATIVE_SAVING_SCREENSHOT");
    if (!screenshot.isEmpty()) {
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(QString::fromUtf8(screenshot)));
    }
    const QByteArray tooltipShot = qgetenv("JOT_NATIVE_TOOLTIP_SCREENSHOT");
    if (!tooltipShot.isEmpty() && localBadge) {
        auto *icons = window->findChild<QObject *>("statusIcons");
        QVERIFY(icons);
        icons->setProperty("hoveredBadge", QVariant::fromValue(localBadge));
        icons->setProperty("tipText", QString("Saved on this device"));
        icons->setProperty("tipVisible", true);
        QTest::qWait(100);
        QVERIFY(tooltip->property("visible").toBool());
        QVERIFY(window->grabWindow().save(QString::fromUtf8(tooltipShot)));
    }

    QCOMPARE(tooltip->property("color").value<QColor>(), QColor("#000000"));
    QCOMPARE(tooltip->property("opacity").toReal(), 1.0);
    auto *tipTextItem = window->findChild<QQuickItem *>("badgeTooltipText");
    QVERIFY(tipTextItem);
    const QFont tipFont = qvariant_cast<QFont>(tipTextItem->property("font"));
    QCOMPARE(tipFont.family(), QString("monospace"));
    QCOMPARE(tipFont.pixelSize(), 13);
    auto *icons = window->findChild<QObject *>("statusIcons");
    QVERIFY(icons);
    icons->setProperty("hoveredBadge", QVariant::fromValue(cloudBadgeItem));
    icons->setProperty("tipText", QString("Cloud backup is off — sign in to enable"));
    icons->setProperty("tipVisible", true);
    QTest::qWait(50);
    QVERIFY(icons->property("tooltipLeftEdge").toReal() >= 0);
    QVERIFY(icons->property("tooltipRightEdge").toReal() <= window->width());
    const QByteArray cloudShot = qgetenv("JOT_NATIVE_CLOUD_TOOLTIP_SCREENSHOT");
    if (!cloudShot.isEmpty())
        QVERIFY(window->grabWindow().save(QString::fromUtf8(cloudShot)));
    icons->setProperty("tipVisible", false);

    QVERIFY(backend.flush());
    QVERIFY(!backend.saving());
    QVERIFY(backend.locallySaved());
    QCOMPARE(backend.localTooltip(), QString("Saved on this device"));
    QVERIFY(!localSpinner->property("visible").toBool());

    Backend other(path);
    other.edit("changed elsewhere");
    QVERIFY(other.flush());
    backend.edit("conflicting local edit");
    QVERIFY(!backend.flush());
    QVERIFY(!backend.saving());
    QVERIFY(backend.localError());
    QCOMPARE(backend.localTooltip(), QString("Local save failed"));
}

void NativeTests::startupRuleInstallsAndReportsFailure() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString helperPath = dir.filePath("hyprctl");
    const QString logPath = dir.filePath("rule-calls");
    QFile helper(helperPath);
    QVERIFY(helper.open(QIODevice::WriteOnly));
    helper.write(
        "#!/usr/bin/python3\n"
        "import os, sys\n"
        "with open(os.environ['JOT_TEST_HYPR_RULE_LOG'], 'a') as log:\n"
        "    log.write(' '.join(sys.argv[1:]) + '\\n')\n"
        "if os.environ.get('JOT_TEST_HYPR_RULE_FAIL') == '1':\n"
        "    sys.stdout.write('rejected')\n"
        "    sys.exit(3)\n"
        "print('ok')\n");
    helper.close();
    QVERIFY(helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    qputenv("JOT_TEST_HYPR_RULE_LOG", logPath.toUtf8());
    qputenv("JOT_TEST_HYPR_RULE_FAIL", "0");

    QString error;
    QVERIFY(installJotPlacementRule(helperPath, &error));
    QVERIFY(error.isEmpty());
    QFile log(logPath);
    QVERIFY(log.open(QIODevice::ReadOnly));
    const QByteArray calls = log.readAll();
    log.close();
    QVERIFY(calls.contains("eval "));
    QVERIFY(calls.contains("jot-placement"));
    QVERIFY(calls.contains("match={class='^jot$'}"));
    QVERIFY(calls.contains("float=true"));
    QVERIFY(calls.contains("(monitor_w-window_w-24) 24"));

    qputenv("JOT_TEST_HYPR_RULE_FAIL", "1");
    QVERIFY(!installJotPlacementRule(helperPath, &error));
    QVERIFY(!error.isEmpty());
    qunsetenv("JOT_TEST_HYPR_RULE_FAIL");
    qunsetenv("JOT_TEST_HYPR_RULE_LOG");
}

void NativeTests::placementTargetsOnlyPrototypeAndCancelsWhenHidden() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString helperPath = dir.filePath("hyprctl");
    const QString logPath = dir.filePath("calls");
    QFile helper(helperPath);
    QVERIFY(helper.open(QIODevice::WriteOnly));
    helper.write(
        "#!/usr/bin/python3\n"
        "import json, os, sys\n"
        "with open(os.environ['JOT_TEST_HYPR_LOG'], 'a') as log:\n"
        "    log.write(' '.join(sys.argv[1:]) + '\\n')\n"
        "if sys.argv[1:] == ['-j', 'clients']:\n"
        "    mode = os.environ['JOT_TEST_HYPR_MODE']\n"
        "    name = 'jot' if mode == 'other' else 'jot-native-prototype'\n"
        "    client = {'pid': int(os.environ['JOT_TEST_HYPR_PID']), 'class': name, 'mapped': True, 'floating': mode != 'tiled', 'size': [486, 353], 'fullscreen': 2 if mode == 'fullscreen' else 0}\n"
        "    at = os.environ.get('JOT_TEST_HYPR_AT')\n"
        "    if at: client['at'] = [int(v) for v in at.split(',')]\n"
        "    print(json.dumps([client]))\n"
        "else:\n"
        "    print('ok')\n");
    helper.close();
    QVERIFY(helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    qputenv("JOT_TEST_HYPR_LOG", logPath.toUtf8());
    qputenv("JOT_TEST_HYPR_PID", QByteArray::number(QCoreApplication::applicationPid()));
    qputenv("JOT_TEST_HYPR_MODE", "own");
    {
        QQuickWindow disabled;
        Placement disabledPlacement(&disabled, false, helperPath);
        disabled.show();
        disabledPlacement.summon();
        QTest::qWait(100);
        QVERIFY(!QFile::exists(logPath));
        disabled.hide();
    }
    QQuickWindow window;
    window.resize(480, 320);
    Placement placement(&window, true, helperPath);
    window.show();
    auto contents = [&]() {
        QFile log(logPath);
        return log.open(QIODevice::ReadOnly) ? log.readAll() : QByteArray();
    };
    QTRY_VERIFY(contents().contains("eval local w="));
    const QByteArray calls = contents();
    QVERIFY(calls.contains("pid:" + QByteArray::number(QCoreApplication::applicationPid())));
    QVERIFY(calls.contains("window.move"));
    const QRect screen = window.screen()->geometry();
    const QByteArray expectedPosition = "x="
        + QByteArray::number(screen.x() + qMax(0, screen.width() - 486 - 24))
        + ",y=" + QByteArray::number(screen.y() + 24);
    QVERIFY(calls.contains(expectedPosition));
    QVERIFY(!calls.contains("window.float"));
    QVERIFY(!calls.contains("window.resize"));
    QVERIFY(calls.contains("hl.dsp.focus"));
    QVERIFY(calls.contains("alter_zorder({mode='top',window=w})"));
    QVERIFY(calls.contains("hl.dsp.cursor.move"));
    QVERIFY(calls.contains("w.floating and w.fullscreen==0"));
    QVERIFY(!calls.contains("bring_to_top"));
    QVERIFY(!calls.contains("class:jot"));

    // Execute the generated Lua with a simulated Hyprland state. A window can
    // change between the JSON lookup and eval; recheck it inside the compositor.
    const int scriptStart = calls.indexOf("eval ") + 5;
    QVERIFY(scriptStart >= 5);
    const QByteArray script = calls.mid(scriptStart).split('\n').first();
    const QByteArray prefix =
        "local w={pid=" + QByteArray::number(QCoreApplication::applicationPid())
        + ",class='jot-native-prototype',mapped=true,hidden=false,floating=true,fullscreen=0};"
          "local calls={};"
          "local function action(n,t) assert(t.window==w); return n end;"
          "hl={get_window=function() return w end,dispatch=function(a) table.insert(calls,a) end,"
          "dsp={focus=function(t) return action('focus',t) end,"
          "cursor={move=function(t) assert(t.x and t.y); return 'cursor' end},window={"
          "move=function(t) return action('move',t) end,"
          "alter_zorder=function(t) return action('top',t) end}}};";
    for (const QByteArray &change : {QByteArray(), QByteArray("w.floating=false;"),
                                     QByteArray("w.fullscreen=2;"), QByteArray("w.mapped=false;"),
                                     QByteArray("w.hidden=true;"), QByteArray("w.class='jot';"),
                                     QByteArray("w.pid=1;")}) {
        QProcess lua;
        lua.start("lua", {"-e", QString::fromUtf8(prefix + change + script
                                                    + ";print(table.concat(calls,','))")});
        QVERIFY(lua.waitForFinished(5000));
        QCOMPARE(lua.exitCode(), 0);
        QCOMPARE(lua.readAllStandardOutput().trimmed(), change.isEmpty()
                     ? QByteArray("move,top,focus,cursor") : QByteArray());
    }

    // If a startup rule already placed the window, no move is dispatched.
    const QByteArray atValue = QByteArray::number(screen.x() + qMax(0, screen.width() - 486 - 24))
        + "," + QByteArray::number(screen.y() + 24);
    qputenv("JOT_TEST_HYPR_AT", atValue);
    QVERIFY(QFile::remove(logPath));
    window.hide();
    window.show();
    QTRY_VERIFY(contents().contains("eval "));
    const QByteArray atCalls = contents();
    QVERIFY(!atCalls.contains("window.move"));
    QVERIFY(atCalls.contains("hl.dsp.focus"));
    qunsetenv("JOT_TEST_HYPR_AT");
    window.hide();
    QVERIFY(QFile::remove(logPath));
    qputenv("JOT_TEST_HYPR_MODE", "other");
    window.show();
    QTRY_VERIFY(contents().contains("-j clients"));
    window.hide();
    // Let any canceled subprocess finish before taking the baseline.
    QTest::qWait(100);
    const QByteArray afterHide = contents();
    QTest::qWait(300);
    QCOMPARE(contents(), afterHide);
    QVERIFY(!afterHide.contains("eval "));
    QVERIFY(QFile::remove(logPath));
    qputenv("JOT_TEST_HYPR_MODE", "fullscreen");
    window.show();
    QTRY_VERIFY(contents().contains("-j clients"));
    QTest::qWait(150);
    window.hide();
    QVERIFY(!contents().contains("eval "));
    QVERIFY(QFile::remove(logPath));
    qputenv("JOT_TEST_HYPR_MODE", "tiled");
    window.show();
    QTRY_VERIFY(contents().contains("-j clients"));
    QTest::qWait(150);
    window.hide();
    QVERIFY(!contents().contains("eval "));
    qunsetenv("JOT_TEST_HYPR_LOG");
    qunsetenv("JOT_TEST_HYPR_PID");
    qunsetenv("JOT_TEST_HYPR_MODE");
}

void NativeTests::fixedWindowHintsProtectFullscreenOnMap() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("pad.db");
    QVERIFY(createDatabase(path));
    Backend backend(path);
    backend.setStartHidden(true);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    QVERIFY(window);
    QVERIFY(!window->isVisible());
    QCOMPARE(window->minimumSize(), QSize(480, 320));
    QCOMPARE(window->maximumSize(), QSize(480, 320));

    // Model Hyprland 0.56's mapWindow: fixed Wayland size hints cause
    // shouldBeFloated(), and only non-floating windows take fullscreen.
    bool browserFullscreen = true;
    auto simulateMap = [&]() {
        const QSize min = window->minimumSize(), max = window->maximumSize();
        const bool floating = min.width() > 0 && min.height() > 0
            && (min.width() == max.width() || min.height() == max.height());
        if (!floating) browserFullscreen = false;
        return floating;
    };
    for (int i = 0; i < 2; ++i) {
        QVERIFY(simulateMap());
        QVariant result;
        QVERIFY(QMetaObject::invokeMethod(window, "handleCommand", Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, QVariant("show"))));
        QVERIFY(window->isVisible());
        QVERIFY(browserFullscreen);
        QVERIFY(QMetaObject::invokeMethod(window, "handleCommand", Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, QVariant("hide"))));
        QVERIFY(!window->isVisible());
    }
}

int main(int argc, char **argv) {
    qputenv("TMPDIR", "/tmp/opencode");
    QTemporaryDir config;
    if (!config.isValid()) return 1;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QGuiApplication app(argc, argv);
    NativeTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "native_tests.moc"
