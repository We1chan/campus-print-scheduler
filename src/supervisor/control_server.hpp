#pragma once
#include <QJsonObject>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <functional>

// Optional per-job control endpoint. No persistent queue or global service is introduced.
class ControlServer : public QObject {
public:
    ControlServer(QString jobId, QString token, int claimAttempt, int leaseMs, QObject* parent = nullptr);
    bool listen();
    quint16 port() const { return server_.serverPort(); }
    void stopLease() { lease_.stop(); }
    std::function<QJsonObject()> snapshot;
    std::function<bool(const QString&, QString&)> execute;
    std::function<void()> leaseExpired;
private:
    void acceptConnections();
    void command(QTcpSocket* socket, const QByteArray& line);
    void send(QTcpSocket* socket, QJsonObject object);
    QString jobId_;
    QString token_;
    int claimAttempt_;
    int leaseMs_;
    int clients_ = 0;
    QTcpServer server_;
    QTimer lease_;
};
