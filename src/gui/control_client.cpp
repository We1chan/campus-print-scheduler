#include "control_client.hpp"
#include <QHostAddress>
#include <QJsonDocument>
#include <QUuid>
#include <algorithm>

namespace cps::gui {
ControlClient::ControlClient(QString jobId, QString token, int claimAttempt, QObject* parent)
    : QObject(parent), jobId_(std::move(jobId)), token_(std::move(token)), claimAttempt_(claimAttempt) {
    retry_.setSingleShot(true);
    responseDeadline_.setSingleShot(true);
    heartbeat_.setInterval(1000);
    socket_.setReadBufferSize(16 * 1024 + 1);
    connect(&retry_, &QTimer::timeout, this, &ControlClient::connectNow);
    connect(&heartbeat_, &QTimer::timeout, this, [this] { if (ready_) sendCommand("ping"); });
    connect(&responseDeadline_, &QTimer::timeout, this, [this] { socket_.abort(); reconnect(); });
    connect(&socket_, &QTcpSocket::connected, this, [this] { sendCommand("subscribe"); });
    connect(&socket_, &QTcpSocket::readyRead, this, &ControlClient::readMessages);
    connect(&socket_, &QTcpSocket::disconnected, this, &ControlClient::reconnect);
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) { reconnect(); });
}
void ControlClient::start(quint16 port) {
    if (active_) return;
    port_ = port;
    active_ = true;
    connectNow();
}
void ControlClient::stop() {
    active_ = ready_ = false;
    retry_.stop(); heartbeat_.stop(); responseDeadline_.stop();
    socket_.abort();
}
void ControlClient::connectNow() {
    if (!active_) return;
    buffer_.clear();
    emit connectionChanged(false, failures_ ? QStringLiteral("控制连接重连中…") : QStringLiteral("正在连接控制通道…"));
    socket_.connectToHost(QHostAddress::LocalHost, port_);
    responseDeadline_.start(2500);
}
void ControlClient::reconnect() {
    if (!active_ || retry_.isActive()) return;
    ready_ = false;
    heartbeat_.stop(); responseDeadline_.stop();
    ++failures_;
    if (failures_ > 8) {
        active_ = false;
        emit connectionChanged(false, QStringLiteral("控制连接失败；等待执行器结束确认"));
        return;
    }
    emit connectionChanged(false, QStringLiteral("控制连接断开 · 自动重连 %1/8").arg(failures_));
    retry_.start(std::min(2000, 200 * failures_));
}
QString ControlClient::sendCommand(const QString& command) {
    if (!active_ || socket_.state() != QAbstractSocket::ConnectedState) return {};
    const auto requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject object{{"schema_version", 1}, {"job_id", jobId_}, {"claim_attempt", claimAttempt_},
        {"token", token_}, {"request_id", requestId}, {"command", command}};
    socket_.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
    return requestId;
}
void ControlClient::simulateDisconnect() {
    if (active_) { socket_.abort(); reconnect(); }
}
void ControlClient::readMessages() {
    buffer_.append(socket_.readAll());
    qsizetype newline = -1;
    while ((newline = buffer_.indexOf('\n')) >= 0) {
        if (newline > 16 * 1024) { socket_.abort(); return; }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(buffer_.left(newline), &error);
        buffer_.remove(0, newline + 1);
        const auto object = doc.object();
        const auto type = object.value("type").toString();
        if (error.error != QJsonParseError::NoError || !doc.isObject() ||
            object.value("schema_version").toDouble() != 1 || object.value("job_id").toString() != jobId_ ||
            object.value("claim_attempt").toDouble() != claimAttempt_ ||
            (type != "control_state" && type != "control_ack")) {
            socket_.abort();
            return;
        }
        responseDeadline_.start(3500);
        if (type == "control_state") {
            ready_ = true; failures_ = 0; heartbeat_.start();
            emit connectionChanged(true, QStringLiteral("控制连接已连接"));
        }
        emit messageReceived(object);
    }
    if (buffer_.size() > 16 * 1024) socket_.abort();
}
} // namespace cps::gui
