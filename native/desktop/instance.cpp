#include "instance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QVariant>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

Instance::Instance(const QString &databasePath, const QString &name, QObject *parent)
    : QObject(parent), m_name(name) {
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty()) return;
    const QString directory = runtime + QLatin1Char('/') + name;
    if (!QDir().mkpath(directory) || !QFile::setPermissions(
            directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return;
    const QByteArray path = QFileInfo(databasePath).canonicalFilePath().toUtf8();
    if (path.isEmpty()) return;
    QByteArray identity = path;
#ifdef Q_OS_UNIX
    struct stat info;
    const QByteArray encodedPath = QFile::encodeName(databasePath);
    if (::stat(encodedPath.constData(), &info) != 0) return;
    identity = QByteArray::number(static_cast<qulonglong>(info.st_dev)) + ':'
        + QByteArray::number(static_cast<qulonglong>(info.st_ino));
#endif
    const auto hash = [](const QByteArray &value) {
        return QString::fromLatin1(
            QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex().left(32));
    };
    m_socketPath = directory + QStringLiteral("/i") + hash(identity);
    m_identityLock = std::make_unique<QLockFile>(m_socketPath + QStringLiteral(".lock"));
    m_pathLock = std::make_unique<QLockFile>(directory + QStringLiteral("/p")
                                            + hash(path) + QStringLiteral(".lock"));
}

Instance::~Instance() {
    delete m_server;
    if (m_identityLock && m_identityLock->isLocked()) m_identityLock->unlock();
    if (m_pathLock && m_pathLock->isLocked()) m_pathLock->unlock();
}

bool Instance::acquire(QString *error) {
    if (!m_pathLock || !m_identityLock) {
        *error = tr("Could not create the private runtime directory for %1.").arg(m_name);
        return false;
    }
    if (!m_pathLock->tryLock(0)) {
        if (m_pathLock->error() != QLockFile::LockFailedError)
            *error = tr("Could not lock the %1 instance.").arg(m_name);
        return false;
    }
    if (m_identityLock->tryLock(0)) return true;
    if (m_identityLock->error() != QLockFile::LockFailedError)
        *error = tr("Could not lock the Jot instance.");
    m_pathLock->unlock();
    return false;
}

bool Instance::send(const QString &command, QString *response) {
    if (m_socketPath.isEmpty()) {
        *response = tr("The Jot runtime directory is unavailable.");
        return false;
    }
    // The first instance may still be starting. A bounded retry avoids taking
    // over its lock or deleting its socket while it is initializing.
    QElapsedTimer deadline;
    deadline.start();
    QLocalSocket socket;
    while (deadline.elapsed() < 5000) {
        socket.abort();
        socket.connectToServer(m_socketPath);
        if (socket.waitForConnected(250)) break;
        QThread::msleep(25);
    }
    if (socket.state() != QLocalSocket::ConnectedState) {
        *response = tr("The existing Jot did not respond.");
        return false;
    }
    socket.write(command.toUtf8() + '\n');
    if (socket.bytesToWrite() && !socket.waitForBytesWritten(2000)) {
        *response = tr("The existing Jot did not acknowledge the command.");
        return false;
    }
    QElapsedTimer responseDeadline;
    responseDeadline.start();
    while (!socket.canReadLine() && responseDeadline.elapsed() < 5000) {
        const int remaining = 5000 - static_cast<int>(responseDeadline.elapsed());
        if (!socket.waitForReadyRead(remaining)) break;
    }
    if (!socket.canReadLine()) {
        *response = tr("The existing Jot did not acknowledge the command.");
        return false;
    }
    *response = QString::fromUtf8(socket.readLine()).trimmed();
    return response->startsWith(QStringLiteral("ok"));
}

bool Instance::listen(QObject *window, QString *error) {
    if (!m_identityLock || !m_identityLock->isLocked()) {
        *error = tr("The Jot instance lock is not held.");
        return false;
    }
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    // Only the lock holder may remove a leftover socket after a previous crash.
    QLocalServer::removeServer(m_socketPath);
    if (!m_server->listen(m_socketPath)) {
        *error = tr("Could not start the Jot command server: %1").arg(m_server->errorString());
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this, window] {
        while (QLocalSocket *client = m_server->nextPendingConnection()) {
            client->setParent(m_server);
            connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            QTimer::singleShot(5000, client, [client] { client->disconnectFromServer(); });
            connect(client, &QLocalSocket::readyRead, client, [client, window] {
                if (!client->canReadLine()) return;
                const QString command = QString::fromUtf8(client->readLine(128)).trimmed();
                QVariant result;
                if (!QMetaObject::invokeMethod(window, "handleCommand",
                        Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QVariant(command))))
                    result = QStringLiteral("error command handler unavailable");
                client->write(result.toString().toUtf8() + '\n');
                client->flush();
                client->disconnectFromServer();
            });
        }
    });
    return true;
}
