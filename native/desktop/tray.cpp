#include "tray.h"
#include "backend.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QImage>
#include <QMetaObject>
#include <cstdio>

QDBusArgument &operator<<(QDBusArgument &argument, const TrayImage &image) {
    argument.beginStructure();
    argument << image.width << image.height << image.data;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, TrayImage &image) {
    argument.beginStructure();
    argument >> image.width >> image.height >> image.data;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const TrayTooltip &tooltip) {
    argument.beginStructure();
    argument << tooltip.iconName << tooltip.images << tooltip.title << tooltip.description;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, TrayTooltip &tooltip) {
    argument.beginStructure();
    argument >> tooltip.iconName >> tooltip.images >> tooltip.title >> tooltip.description;
    argument.endStructure();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const TrayMenuLayout &item) {
    argument.beginStructure();
    argument << item.id << item.properties << item.children;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, TrayMenuLayout &item) {
    argument.beginStructure();
    argument >> item.id >> item.properties >> item.children;
    argument.endStructure();
    return argument;
}

static void dispatch(QObject *window, const QString &command) {
    QVariant result;
    if (!QMetaObject::invokeMethod(window, "handleCommand", Q_RETURN_ARG(QVariant, result),
                                   Q_ARG(QVariant, QVariant(command))))
        std::fprintf(stderr, "jot: tray command handler unavailable\n");
}

TrayMenu::TrayMenu(QObject *window, QObject *parent) : QObject(parent), m_window(window) {}

QVariantMap TrayMenu::properties(int id) const {
    if (id == 0) return {{QStringLiteral("children-display"), QStringLiteral("submenu")}};
    if (id == 1) return {{QStringLiteral("label"), tr("Show")}};
    if (id == 2) return {{QStringLiteral("label"), tr("Settings")}};
    if (id == 3) return {{QStringLiteral("label"), tr("Quit")}};
    return {};
}

uint TrayMenu::GetLayout(int parentId, int depth, const QStringList &names, TrayMenuLayout &layout) {
    layout.id = parentId;
    layout.properties = properties(parentId);
    if (!names.isEmpty()) {
        for (const QString &key : layout.properties.keys())
            if (!names.contains(key)) layout.properties.remove(key);
    }
    layout.children.clear();
    if (parentId == 0 && depth != 0) {
        for (int id = 1; id <= 3; ++id) {
            TrayMenuLayout child;
            child.id = id;
            child.properties = properties(id);
            if (!names.isEmpty()) {
                for (const QString &key : child.properties.keys())
                    if (!names.contains(key)) child.properties.remove(key);
            }
            // QVariantList already marshals each child as a D-Bus variant.
            layout.children.append(QVariant::fromValue(child));
        }
    }
    return 1;
}

QDBusVariant TrayMenu::GetProperty(int id, const QString &name) {
    return QDBusVariant(properties(id).value(name));
}

void TrayMenu::Event(int id, const QString &event, const QDBusVariant &, uint) {
    if (event != QStringLiteral("clicked")) return;
    if (id == 1) dispatch(m_window, QStringLiteral("show"));
    if (id == 2) dispatch(m_window, QStringLiteral("settings"));
    if (id == 3) dispatch(m_window, QStringLiteral("quit"));
}

bool TrayMenu::AboutToShow(int) { return false; }

Tray::Tray(QObject *window, Backend *backend, QObject *parent)
    : QObject(parent), m_window(window), m_backend(backend), m_menu(window), m_watcher(this) {
    qDBusRegisterMetaType<TrayImage>();
    qDBusRegisterMetaType<TrayImages>();
    qDBusRegisterMetaType<TrayTooltip>();
    qDBusRegisterMetaType<TrayMenuLayout>();
    const QImage icon = QImage(QStringLiteral(":/jot-icon.png"))
        .scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_ARGB32);
    if (!icon.isNull()) {
        TrayImage image;
        image.width = icon.width();
        image.height = icon.height();
        image.data.reserve(icon.width() * icon.height() * 4);
        // StatusNotifier pixel data uses network-order ARGB, not host byte order.
        for (int y = 0; y < icon.height(); ++y) {
            for (int x = 0; x < icon.width(); ++x) {
                const QRgb pixel = icon.pixel(x, y);
                image.data.append(char(qAlpha(pixel)));
                image.data.append(char(qRed(pixel)));
                image.data.append(char(qGreen(pixel)));
                image.data.append(char(qBlue(pixel)));
            }
        }
        m_images.append(image);
    }
    m_service = QStringLiteral("org.kde.StatusNotifierItem-%1-1").arg(QCoreApplication::applicationPid());
}

Tray::~Tray() {
    if (!m_exported) return;
    auto bus = QDBusConnection::sessionBus();
    bus.unregisterObject(QStringLiteral("/StatusNotifierItem"));
    bus.unregisterObject(QStringLiteral("/MenuBar"));
    bus.unregisterService(m_service);
}

bool Tray::start(QString *error) {
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || m_images.isEmpty()) {
        *error = tr("Tray unavailable; use the Jot CLI to show or quit.");
        return false;
    }
    const auto flags = QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties;
    if (!bus.registerService(m_service)) {
        *error = tr("Could not register the Jot tray service.");
        return false;
    }
    if (!bus.registerObject(QStringLiteral("/StatusNotifierItem"), this, flags)) {
        bus.unregisterService(m_service);
        *error = tr("Could not export the Jot tray icon.");
        return false;
    }
    if (!bus.registerObject(QStringLiteral("/MenuBar"), &m_menu, flags)) {
        bus.unregisterObject(QStringLiteral("/StatusNotifierItem"));
        bus.unregisterService(m_service);
        *error = tr("Could not export the Jot tray menu.");
        return false;
    }
    m_exported = true;
    m_watcher.setConnection(bus);
    m_watcher.setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    m_watcher.addWatchedService(QStringLiteral("org.kde.StatusNotifierWatcher"));
    connect(&m_watcher, &QDBusServiceWatcher::serviceRegistered, this,
            [this](const QString &) { registerWithWatcher(); });
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this,
            [this](const QString &) {
                ++m_registrationGeneration;
                m_backend->setTrayAvailable(false);
            });
    registerWithWatcher();
    return true;
}

void Tray::registerWithWatcher() {
    const quint64 generation = ++m_registrationGeneration;
    QDBusInterface watcher(QStringLiteral("org.kde.StatusNotifierWatcher"),
                           QStringLiteral("/StatusNotifierWatcher"),
                           QStringLiteral("org.kde.StatusNotifierWatcher"),
                           QDBusConnection::sessionBus());
    auto *call = new QDBusPendingCallWatcher(
        watcher.asyncCall(QStringLiteral("RegisterStatusNotifierItem"), m_service), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<> reply = *call;
        if (generation == m_registrationGeneration) {
            m_backend->setTrayAvailable(!reply.isError());
            if (reply.isError())
                std::fprintf(stderr, "jot: tray host unavailable; use Jot CLI: %s\n",
                             qPrintable(reply.error().message()));
        }
        call->deleteLater();
    });
}

void Tray::Activate(int, int) { dispatch(m_window, QStringLiteral("show")); }
void Tray::ContextMenu(int, int) { dispatch(m_window, QStringLiteral("show")); }
void Tray::SecondaryActivate(int, int) { dispatch(m_window, QStringLiteral("show")); }
void Tray::Scroll(int, const QString &) {}
