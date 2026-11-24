#pragma once

#include <QObject>
#include <QString>
#include <memory>

class QLocalServer;
class QLockFile;

// Instances are isolated by the test database path, never by Jot's live name.
class Instance : public QObject {
    Q_OBJECT
public:
    explicit Instance(const QString &databasePath,
                      const QString &name = QStringLiteral("jot-native-prototype"),
                      QObject *parent = nullptr);
    ~Instance() override;
    bool acquire(QString *error);
    bool send(const QString &command, QString *response);
    bool listen(QObject *window, QString *error);

private:
    QString m_name;
    QString m_socketPath;
    std::unique_ptr<QLockFile> m_pathLock;
    std::unique_ptr<QLockFile> m_identityLock;
    QLocalServer *m_server = nullptr;
};
