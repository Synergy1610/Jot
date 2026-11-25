#include "backend.h"
#include "hyprland_rule.h"
#include "instance.h"
#include "placement.h"
#include "tray.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>
#include <QWindow>
#include <cstdio>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

#ifdef JOT_PRODUCTION
static QString appName() { return QStringLiteral("jot"); }
static QString defaultDatabasePath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/dev.jot.app/pad.db");
}
#else
static QString appName() { return QStringLiteral("jot-native-prototype"); }
#endif

static bool sameFile(const QString &first, const QString &second) {
    const QFileInfo a(first), b(second);
    if (!a.exists() || !b.exists()) return false;
#ifdef Q_OS_UNIX
    struct stat left, right;
    const QByteArray aPath = QFile::encodeName(a.absoluteFilePath());
    const QByteArray bPath = QFile::encodeName(b.absoluteFilePath());
    if (::stat(aPath.constData(), &left) == 0 && ::stat(bPath.constData(), &right) == 0)
        return left.st_dev == right.st_dev && left.st_ino == right.st_ino;
#endif
    return a.canonicalFilePath() == b.canonicalFilePath();
}

static QString resolvedPath(const QString &path) {
    const QFileInfo info(path);
    const QString existing = info.canonicalFilePath();
    if (!existing.isEmpty()) return existing;
    const QString parent = info.dir().canonicalPath();
    if (!parent.isEmpty()) return QDir(parent).filePath(info.fileName());
    return QDir::cleanPath(info.absoluteFilePath());
}

static bool protectedPath(const QString &path, const QString &active, const QString &homeActive) {
    const QString resolved = resolvedPath(path);
    return resolved == resolvedPath(active) || resolved == resolvedPath(homeActive)
        || sameFile(path, active) || sameFile(path, homeActive);
}

int main(int argc, char **argv) {
    // Maintenance commands should work without opening or connecting a window.
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--create" || arg == "--backup-to" || arg.startsWith("--backup-to=")) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            qputenv("QT_ACCESSIBILITY", "0");
            break;
        }
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName(appName());
    app.setDesktopFileName(appName());
    app.setWindowIcon(QIcon(QStringLiteral(":/jot-icon.png")));
    app.setQuitOnLastWindowClosed(false);

    QCommandLineParser parser;
    parser.setApplicationDescription("Isolated Jot native prototype; not a replacement for Jot.");
    parser.addHelpOption();
    parser.addOption({{"d", "db"}, "Path to a disposable test database.", "path"});
    parser.addOption({"hidden", "Start a new prototype instance hidden."});
    parser.addOption({"create", "Create a new disposable database and exit."});
    parser.addOption({"backup-to", "Write a new verified SQLite backup and exit.", "path"});
    parser.addPositionalArgument("command", "show, hide, toggle, or quit (default: show).", "[command]");
    parser.process(app);
    // The prototype keeps placement opt-in. The production build enables it
    // by default after live-screen acceptance, with an explicit opt-out for
    // unusual compositor setups.
#ifdef JOT_PRODUCTION
    const bool placementEnabled =
        !qEnvironmentVariableIsSet("JOT_NATIVE_EXPERIMENTAL_PLACEMENT")
        || qEnvironmentVariableIntValue("JOT_NATIVE_EXPERIMENTAL_PLACEMENT") != 0;
#else
    const bool placementEnabled =
        qEnvironmentVariableIntValue("JOT_NATIVE_EXPERIMENTAL_PLACEMENT") == 1;
#endif
    const bool hyprland = placementEnabled
        && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")
        && QGuiApplication::platformName().startsWith(QStringLiteral("wayland"));
    QString path = parser.value("db");
#ifdef JOT_PRODUCTION
    if (path.isEmpty()) path = defaultDatabasePath();
#else
    if (path.isEmpty()) {
        std::fprintf(stderr, "A disposable test database is required: --db PATH\n");
        return 1;
    }
#endif
    const QString active = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + "/dev.jot.app/pad.db";
    const QString homeActive = QDir::homePath() + "/.config/dev.jot.app/pad.db";
#ifndef JOT_PRODUCTION
    // Avoid touching the active application's data during the prototype phase.
    if (protectedPath(path, active, homeActive)) {
        std::fprintf(stderr, "Refusing to open the live Jot database in the prototype.\n");
        return 1;
    }
#endif
    const QStringList args = parser.positionalArguments();
    if (args.size() > 1 || (!args.isEmpty()
        && !QStringList{"show", "hide", "toggle", "quit"}.contains(args.first()))) {
        std::fprintf(stderr, "Expected one command: show, hide, toggle, or quit.\n");
        return 1;
    }
    const QString command = args.isEmpty() ? QStringLiteral("show") : args.first();
    if (parser.isSet("hidden") && !args.isEmpty()) {
        std::fprintf(stderr, "--hidden cannot be combined with a command.\n");
        return 1;
    }

    if (parser.isSet("create")) {
        if (parser.isSet("hidden") || parser.isSet("backup-to") || !args.isEmpty()) {
            std::fprintf(stderr, "--create cannot be combined with a command, --hidden or --backup-to.\n");
            return 1;
        }
        char *message = nullptr;
        const QByteArray destination = path.toUtf8();
        if (!jot_core_create(destination.constData(), &message)) {
            std::fprintf(stderr, "%s\n", message ? message : "Could not create database.");
            if (message) jot_core_string_free(message);
            return 1;
        }
        std::fprintf(stdout, "New database created.\n");
        return 0;
    }
    if (!QFileInfo(path).isFile()) {
#ifdef JOT_PRODUCTION
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            std::fprintf(stderr, "jot: could not create the config directory for %s\n",
                         qPrintable(path));
            return 1;
        }
        char *message = nullptr;
        const QByteArray destination = path.toUtf8();
        if (!jot_core_create(destination.constData(), &message)) {
            std::fprintf(stderr, "%s\n", message ? message : "Could not create the pad database.");
            if (message) jot_core_string_free(message);
            return 1;
        }
#else
        std::fprintf(stderr, "The disposable database must already exist.\n");
        return 1;
#endif
    }
    if (parser.isSet("backup-to")) {
        if (parser.isSet("hidden") || !args.isEmpty() || parser.value("backup-to").isEmpty()) {
            std::fprintf(stderr, "--backup-to requires a destination and cannot be combined with a command or --hidden.\n");
            return 1;
        }
        if (protectedPath(parser.value("backup-to"), active, homeActive)) {
            std::fprintf(stderr, "Refusing to write a backup at the live Jot database path.\n");
            return 1;
        }
        char *message = nullptr;
        const QByteArray source = path.toUtf8();
        const QByteArray destination = parser.value("backup-to").toUtf8();
        JotCorePad *pad = jot_core_open(source.constData(), &message);
        const bool success = pad && jot_core_backup(pad, destination.constData(), &message);
        if (pad) jot_core_close(pad);
        if (!success) {
            std::fprintf(stderr, "%s\n", message ? message : "Backup failed.");
            if (message) jot_core_string_free(message);
            return 1;
        }
        std::fprintf(stdout, "Backup created and integrity checked.\n");
        return 0;
    }

    Instance instance(path, appName());
    QString error;
    if (!instance.acquire(&error)) {
        if (!error.isEmpty()) {
            std::fprintf(stderr, "%s\n", qPrintable(error));
            return 1;
        }
        if (parser.isSet("hidden")) return 0;
        QString response;
        const bool ok = instance.send(command, &response);
        std::fprintf(ok ? stdout : stderr, "%s\n", qPrintable(response));
        return ok ? 0 : 1;
    }
    if (command == "quit" || command == "hide") return 0;

    Backend backend(path);
    if (!backend.ready()) {
        std::fprintf(stderr, "%s\n", qPrintable(backend.status()));
        return 1;
    }
#ifdef JOT_PRODUCTION
    // A verified backup is kept before the first upgrade. If migration fails,
    // the app exits instead of showing notes in an unknown format.
    const QString configDir = QFileInfo(path).absolutePath();
    const QString safetyBackup = configDir + QStringLiteral("/pad.db.jot-native-backup.db");
    const QString marker = configDir + QStringLiteral("/.jot-native-migrated");
    const bool firstRun = !QFile::exists(marker);
    if (firstRun && !QFile::exists(safetyBackup) && !backend.backupTo(safetyBackup)) {
        std::fprintf(stderr, "jot: could not make a safety backup before migration: %s\n",
                     qPrintable(backend.status()));
        return 1;
    }
    if (!backend.migrateLegacy()) {
        std::fprintf(stderr, "jot: %s\n", qPrintable(backend.status()));
        return 1;
    }
    if (firstRun) {
        QFile markerFile(marker);
        if (markerFile.open(QIODevice::WriteOnly)) markerFile.write("1\n");
    }
#endif
#ifdef JOT_PRODUCTION
    // Install before the first map: on Wayland the compositor chooses the
    // initial position, so a rule is the only way to avoid a visible jump
    // from the centre to the top-right.
    if (hyprland) {
        QString ruleError;
        if (!installJotPlacementRule(QStringLiteral("hyprctl"), &ruleError))
            std::fprintf(stderr, "jot: %s; using runtime placement\n",
                         qPrintable(ruleError));
    }
#endif
    backend.setStartHidden(parser.isSet("hidden"));
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl(QStringLiteral("qrc:/Main.qml")));
    if (engine.rootObjects().isEmpty()) return 1;
    if (!backend.ready() && parser.isSet("hidden")) {
        std::fprintf(stderr, "%s\n", qPrintable(backend.status()));
        return 1;
    }
    if (!instance.listen(engine.rootObjects().first(), &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    QWindow *window = qobject_cast<QWindow *>(engine.rootObjects().first());
    if (!window) return 1;
    Placement placement(window, hyprland, QStringLiteral("hyprctl"), appName());
    QObject::connect(window, SIGNAL(summoned()), &placement, SLOT(summon()));
    if (window->isVisible()) placement.summon();
    Tray tray(engine.rootObjects().first(), &backend);
    if (!tray.start(&error))
        std::fprintf(stderr, "jot: %s\n", qPrintable(error));
    std::fprintf(stdout, "ready\n");
    std::fflush(stdout);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &backend, [&backend] {
        backend.flush();
    });
    return app.exec();
}
