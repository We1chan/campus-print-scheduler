#pragma once
#include <QJsonObject>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>

namespace cps::gui {
class ControlClient : public QObject {
    Q_OBJECT
public:
    ControlClient(QString jobId, QString token, int claimAttempt, QObject* parent = nullptr);
    ~ControlClient() override { stop(); }
    void start(quint16 port);
    void stop();
    bool isReady() const { return ready_; }
    QString sendCommand(const QString& command);
    void simulateDisconnect();
signals:
    void connectionChanged(bool ready, const QString& status);
    void messageReceived(const QJsonObject& message);
private:
    void connectNow();
    void reconnect();
    void readMessages();
    QString jobId_;
    QString token_;
    int claimAttempt_;
    quint16 port_ = 0;
    QTcpSocket socket_;
    QTimer retry_;
    QTimer heartbeat_;
    QTimer responseDeadline_;
    QByteArray buffer_;
    bool active_ = false;
    bool ready_ = false;
    int failures_ = 0;
};
} // namespace cps::gui
