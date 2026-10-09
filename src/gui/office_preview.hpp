#pragma once
#include <QObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

namespace cps::gui {
// A local, isolated conversion session. The GUI loads the resulting PDF; no
// document bytes or temporary paths are added to the coordinator protocol.
class OfficePreview : public QObject {
    Q_OBJECT
public:
    explicit OfficePreview(QObject *parent = nullptr);
    ~OfficePreview() override;
    static bool supports(const QString &path);
    static QString executable();
    void start(const QString &path);
    void cancel();
    bool busy() const { return busy_; }
    QString temporaryPath() const;
signals:
    void ready(const QString &pdfPath);
    void failed(const QString &message);
private:
    void finish(int exitCode, QProcess::ExitStatus status);
    void failLater(const QString &message);
    void stopProcess();
    QProcess process_;
    QTimer deadline_;
    std::unique_ptr<QTemporaryDir> temporary_;
    QString pdfPath_;
    bool busy_ = false;
    quint64 generation_ = 0;
#ifdef Q_OS_WIN
    void *jobHandle_ = nullptr;
#endif
};
} // namespace cps::gui
