#pragma once

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QVariant>

class Backend;

struct TrayImage {
    int width = 0;
    int height = 0;
    QByteArray data;
};
using TrayImages = QList<TrayImage>;
Q_DECLARE_METATYPE(TrayImage)
Q_DECLARE_METATYPE(TrayImages)
QDBusArgument &operator<<(QDBusArgument &argument, const TrayImage &image);
const QDBusArgument &operator>>(const QDBusArgument &argument, TrayImage &image);

struct TrayTooltip {
    QString iconName;
    TrayImages images;
    QString title;
    QString description;
};
Q_DECLARE_METATYPE(TrayTooltip)
QDBusArgument &operator<<(QDBusArgument &argument, const TrayTooltip &tooltip);
const QDBusArgument &operator>>(const QDBusArgument &argument, TrayTooltip &tooltip);

struct TrayMenuLayout {
    int id = 0;
    QVariantMap properties;
    QVariantList children;
};
Q_DECLARE_METATYPE(TrayMenuLayout)
QDBusArgument &operator<<(QDBusArgument &argument, const TrayMenuLayout &item);
const QDBusArgument &operator>>(const QDBusArgument &argument, TrayMenuLayout &item);

class TrayMenu : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
    Q_PROPERTY(uint Version READ version CONSTANT)
    Q_PROPERTY(QString Status READ status CONSTANT)
    Q_PROPERTY(QString TextDirection READ textDirection CONSTANT)
public:
    explicit TrayMenu(QObject *window, QObject *parent = nullptr);
    uint version() const { return 3; }
    QString status() const { return QStringLiteral("normal"); }
    QString textDirection() const { return QStringLiteral("ltr"); }

public slots:
    uint GetLayout(int parentId, int depth, const QStringList &names, TrayMenuLayout &layout);
    QDBusVariant GetProperty(int id, const QString &name);
    void Event(int id, const QString &event, const QDBusVariant &data, uint timestamp);
    bool AboutToShow(int id);

private:
    QVariantMap properties(int id) const;
    QObject *m_window;
};

class Tray : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Category READ category CONSTANT)
    Q_PROPERTY(QString Id READ id CONSTANT)
    Q_PROPERTY(QString Title READ title CONSTANT)
    Q_PROPERTY(QString Status READ status CONSTANT)
    Q_PROPERTY(uint WindowId READ windowId CONSTANT)
    Q_PROPERTY(QString IconName READ iconName CONSTANT)
    Q_PROPERTY(TrayImages IconPixmap READ iconPixmap CONSTANT)
    Q_PROPERTY(TrayTooltip ToolTip READ toolTip CONSTANT)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu CONSTANT)
    Q_PROPERTY(QDBusObjectPath Menu READ menu CONSTANT)
public:
    explicit Tray(QObject *window, Backend *backend, QObject *parent = nullptr);
    ~Tray() override;
    bool start(QString *error);
    QString category() const { return QStringLiteral("ApplicationStatus"); }
    QString id() const { return QStringLiteral("jot-native-prototype"); }
    QString title() const { return tr("Jot native prototype"); }
    QString status() const { return QStringLiteral("Active"); }
    uint windowId() const { return 0; }
    QString iconName() const { return {}; }
    TrayImages iconPixmap() const { return m_images; }
    TrayTooltip toolTip() const {
        return {QString(), {}, tr("Jot native prototype"), tr("Isolated test instance")};
    }
    bool itemIsMenu() const { return false; }
    QDBusObjectPath menu() const { return QDBusObjectPath(QStringLiteral("/MenuBar")); }

public slots:
    void Activate(int x, int y);
    void ContextMenu(int x, int y);
    void SecondaryActivate(int x, int y);
    void Scroll(int delta, const QString &orientation);

private:
    void registerWithWatcher();
    QObject *m_window;
    Backend *m_backend;
    TrayMenu m_menu;
    TrayImages m_images;
    QString m_service;
    QDBusServiceWatcher m_watcher;
    bool m_exported = false;
    quint64 m_registrationGeneration = 0;
};
