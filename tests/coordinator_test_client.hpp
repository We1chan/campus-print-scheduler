#pragma once
#include "cps/runtime/state_store.hpp"
#include <QElapsedTimer>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

// Tests own unique state directories and stop only the coordinator in that directory.
class CoordinatorClient {
public:
    explicit CoordinatorClient(const QString& root) : endpoint_(cps::coordinatorSocketName(root)) {}
    QJsonObject request(const QString& type, QJsonObject payload = {}) {
        if (socket_.state() != QLocalSocket::ConnectedState) {
            socket_.abort(); socket_.connectToServer(endpoint_);
            if (!socket_.waitForConnected(1000)) return {};
        }
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        payload.insert("schema_version", 1); payload.insert("request_id", id); payload.insert("type", type);
        socket_.write(QJsonDocument(payload).toJson(QJsonDocument::Compact) + '\n');
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 5000) {
            while (socket_.canReadLine()) {
                const auto object = QJsonDocument::fromJson(socket_.readLine()).object();
                if (object.value("request_id").toString() == id) return object;
            }
            if (socket_.state() != QLocalSocket::ConnectedState) break;
            socket_.waitForReadyRead(50);
        }
        return {};
    }
    void disconnect() { socket_.abort(); }
    void stop() {
        const auto state = request("snapshot");
        if (state.isEmpty()) return;
        for (const auto& item : state.value("jobs").toArray()) {
            const auto j = item.toObject();
            if (j.value("state").toString() == "running") request("cancel", {{"job_id", j.value("job_id")}});
        }
        request("shutdown");
        if (socket_.state() == QLocalSocket::ConnectedState) socket_.waitForDisconnected(10000);
    }
private:
    QString endpoint_;
    QLocalSocket socket_;
};
class BackendDirectory : public QTemporaryDir {
public:
    using QTemporaryDir::QTemporaryDir;
    ~BackendDirectory() {
        CoordinatorClient(path()).stop();
        QElapsedTimer timer; timer.start();
        // A disconnected client does not imply the server has finished its final save.
        const auto lock = QDir(path()).filePath(".coordinator.lock");
        while (QFileInfo::exists(lock) && timer.elapsed() < 10000) QThread::msleep(20);
        if (QFileInfo::exists(lock)) {
            qWarning("Test coordinator did not release its state directory; preserving it for diagnosis");
            setAutoRemove(false);
        }
    }
};
