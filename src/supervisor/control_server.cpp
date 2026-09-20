#include "control_server.hpp"
#include <QHostAddress>
#include <QJsonDocument>
#include <memory>

ControlServer::ControlServer(QString jobId, QString token, int claimAttempt, int leaseMs, QObject* parent)
    : QObject(parent), jobId_(std::move(jobId)), token_(std::move(token)),
      claimAttempt_(claimAttempt), leaseMs_(leaseMs) {
    lease_.setSingleShot(true);
    connect(&lease_, &QTimer::timeout, this, [this] { if (leaseExpired) leaseExpired(); });
    connect(&server_, &QTcpServer::newConnection, this, &ControlServer::acceptConnections);
}
bool ControlServer::listen() {
    if (!server_.listen(QHostAddress::LocalHost, 0)) return false;
    lease_.start(leaseMs_);
    return true;
}
void ControlServer::acceptConnections() {
    while (server_.hasPendingConnections()) {
        auto* socket = server_.nextPendingConnection();
        if (clients_ >= 4) { socket->abort(); socket->deleteLater(); continue; }
        ++clients_;
        socket->setReadBufferSize(16 * 1024 + 1);
        auto pending = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, pending] {
            pending->append(socket->readAll());
            qsizetype newline = -1;
            while ((newline = pending->indexOf('\n')) >= 0) {
                if (newline > 16 * 1024) { socket->abort(); return; }
                const auto line = pending->left(newline);
                pending->remove(0, newline + 1);
                command(socket, line);
            }
            if (pending->size() > 16 * 1024) socket->abort();
        });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            --clients_;
            socket->deleteLater();
        });
        QTimer::singleShot(2000, socket, [socket] {
            if (!socket->property("authenticated").toBool()) socket->abort();
        });
    }
}
void ControlServer::send(QTcpSocket* socket, QJsonObject object) {
    object.insert("schema_version", 1);
    object.insert("job_id", jobId_);
    object.insert("claim_attempt", claimAttempt_);
    if (socket->bytesToWrite() > 64 * 1024) { socket->abort(); return; }
    socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
    socket->flush();
}
void ControlServer::command(QTcpSocket* socket, const QByteArray& line) {
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(line, &error);
    const auto value = doc.object();
    const auto id = value.value("request_id").toString();
    const auto action = value.value("command").toString();
    const bool authorized = error.error == QJsonParseError::NoError && doc.isObject() &&
        value.value("schema_version").toDouble() == 1 && value.value("job_id").toString() == jobId_ &&
        value.value("token").toString() == token_ && value.value("claim_attempt").toDouble() == claimAttempt_ &&
        !id.isEmpty() && id.size() <= 80;
    if (!authorized) {
        send(socket, {{"type", "control_ack"}, {"request_id", id.left(80)}, {"command", action.left(80)},
            {"accepted", false}, {"reason", "invalid command, token or claim attempt"}});
        return;
    }
    socket->setProperty("authenticated", true);
    lease_.start(leaseMs_);
    if (action == "subscribe") {
        auto state = snapshot ? snapshot() : QJsonObject{};
        state.insert("type", "control_state");
        send(socket, state);
        return;
    }
    QString reason;
    const bool accepted = action == "ping" || (execute && execute(action, reason));
    send(socket, {{"type", "control_ack"}, {"request_id", id}, {"command", action},
        {"accepted", accepted}, {"reason", reason}});
}
