#pragma once

#include <QObject>
#include <QString>

class Hotkey : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString snippet READ snippet CONSTANT)
    Q_PROPERTY(QString configPath READ configPath CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit Hotkey(const QString &directory, QObject *parent = nullptr);
    QString snippet() const;
    QString configPath() const;
    QString status() const { return m_status; }
    Q_INVOKABLE void refresh();
    Q_INVOKABLE bool copy();
    Q_INVOKABLE bool openConfig();

signals:
    void statusChanged();

private:
    void setStatus(const QString &status);
    QString m_directory;
    QString m_status;
};
