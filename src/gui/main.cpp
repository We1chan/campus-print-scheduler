#include "cps/core/scheduler.hpp"
#include "cps/runtime/job_io.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>
#include <QCloseEvent>

class PrintWindow : public QMainWindow {
public:
    PrintWindow() {
        setWindowTitle(QStringLiteral("校园共享打印调度 · 开发起点"));
        resize(1020, 720);
        auto* central = new QWidget(this);
        auto* layout = new QVBoxLayout(central);
        auto* heading = new QLabel(QStringLiteral("校园共享打印任务调度"));
        heading->setStyleSheet("font-size: 24px; font-weight: 600; padding: 12px 0;");
        layout->addWidget(heading);
        auto* note = new QLabel(QStringLiteral("2 台模拟打印机 · 独立工作进程 · FCFS / SJF · 崩溃自动重试\n"
            "当前队列保存在内存中；输出与任务文件落盘。持久队列、取消任务和统计由三人继续实现。"));
        note->setWordWrap(true);
        layout->addWidget(note);
        auto* form = new QFormLayout;
        title_ = new QLineEdit(QStringLiteral("操作系统课设文档"));
        pages_ = new QSpinBox;
        pages_->setRange(1, 1000);
        pages_->setValue(8);
        copies_ = new QSpinBox;
        copies_->setRange(1, 10);
        policy_ = new QComboBox;
        policy_->addItems({QStringLiteral("先来先服务 FCFS"), QStringLiteral("短作业优先 SJF")});
        form->addRow(QStringLiteral("文档名称"), title_);
        form->addRow(QStringLiteral("页数"), pages_);
        form->addRow(QStringLiteral("份数"), copies_);
        form->addRow(QStringLiteral("调度策略（仅影响等待任务）"), policy_);
        layout->addLayout(form);
        auto* submit = new QPushButton(QStringLiteral("提交模拟打印任务"));
        layout->addWidget(submit);
        table_ = new QTableWidget(0, 4);
        table_->setHorizontalHeaderLabels({QStringLiteral("任务"), QStringLiteral("总页数"),
            QStringLiteral("状态"), QStringLiteral("任务 ID")});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        layout->addWidget(table_);
        log_ = new QPlainTextEdit;
        log_->setReadOnly(true);
        log_->setMaximumBlockCount(1000);
        layout->addWidget(log_);
        stateDirectory_ = cps::defaultStateDirectory();
        auto* location = new QLabel(QStringLiteral("运行数据目录：") + stateDirectory_);
        location->setWordWrap(true);
        location->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(location);
        setCentralWidget(central);
        connect(submit, &QPushButton::clicked, this, [this] {
            cps::Job job;
            job.id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            job.title = title_->text().trimmed().toUtf8().toStdString();
            job.pages = pages_->value();
            job.copies = copies_->value();
            job.submittedMs = QDateTime::currentMSecsSinceEpoch();
            if (title_->text().size() > 200 || !scheduler_.submit(job)) {
                QMessageBox::warning(this, QStringLiteral("提交失败"), QStringLiteral("请检查文档名称和页数。"));
                return;
            }
            refresh();
            dispatch();
        });
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (active_ > 0) {
            QMessageBox::information(this, QStringLiteral("仍在打印"),
                QStringLiteral("请等待当前队列完成后退出；取消与后台脱离功能见成员分工。"));
            event->ignore();
        } else {
            event->accept();
        }
    }

private:
    void refresh() {
        const auto jobs = scheduler_.snapshot();
        table_->setRowCount(static_cast<int>(jobs.size()));
        int row = 0;
        for (const auto& job : jobs) {
            QString state;
            switch (job.state) {
            case cps::JobState::Queued: state = QStringLiteral("等待中"); break;
            case cps::JobState::Running: state = QStringLiteral("打印中"); break;
            case cps::JobState::Completed: state = QStringLiteral("已完成"); break;
            case cps::JobState::Failed: state = QStringLiteral("失败"); break;
            }
            table_->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(job.title.data(),
                static_cast<qsizetype>(job.title.size()))));
            table_->setItem(row, 1, new QTableWidgetItem(QString::number(job.sheets())));
            table_->setItem(row, 2, new QTableWidgetItem(state));
            table_->setItem(row, 3, new QTableWidgetItem(QString::fromStdString(job.id)));
            ++row;
        }
    }

    void dispatch() {
        while (active_ < 2) {
            const auto job = scheduler_.claim(policy_->currentIndex() == 0 ? cps::Policy::Fcfs :
                                             cps::Policy::ShortestJobFirst);
            if (!job) break;
            const QString jobsDirectory = QDir(stateDirectory_).filePath("jobs");
            const QString outputDirectory = QDir(stateDirectory_).filePath("output");
            const QString path = QDir(jobsDirectory).filePath(QString::fromStdString(job->id) + ".json");
            QString error;
            if (!QDir().mkpath(jobsDirectory) || !cps::writeJob(path, *job, error)) {
                scheduler_.finish(job->id, false);
                log_->appendPlainText(QStringLiteral("写入任务失败：") + error);
                continue;
            }
            auto* process = new QProcess(this);
            process->setProgram(cps::siblingExecutable("print-supervisor"));
            process->setArguments({"--job", path, "--output-dir", outputDirectory, "--page-ms", "150"});
            connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
                log_->appendPlainText(QString::fromUtf8(process->readAllStandardOutput()).trimmed());
            });
            connect(process, &QProcess::readyReadStandardError, this, [this, process] {
                log_->appendPlainText(QString::fromUtf8(process->readAllStandardError()).trimmed());
            });
            const auto id = job->id;
            connect(process, &QProcess::errorOccurred, this, [this, process, id](QProcess::ProcessError code) {
                if (code == QProcess::FailedToStart) {
                    scheduler_.finish(id, false);
                    --active_;
                    log_->appendPlainText(process->errorString());
                    process->deleteLater();
                    refresh();
                    QTimer::singleShot(0, this, [this] { dispatch(); });
                }
            });
            connect(process, &QProcess::finished, this, [this, process, id](int code, QProcess::ExitStatus status) {
                scheduler_.finish(id, status == QProcess::NormalExit && code == 0);
                --active_;
                process->deleteLater();
                refresh();
                dispatch();
            });
            ++active_;
            process->start();
        }
        refresh();
    }

    cps::Scheduler scheduler_;
    int active_ = 0;
    QString stateDirectory_;
    QLineEdit* title_ = nullptr;
    QSpinBox* pages_ = nullptr;
    QSpinBox* copies_ = nullptr;
    QComboBox* policy_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("CampusPrint");
    QApplication::setApplicationName("CampusPrintScheduler");
    PrintWindow window;
    window.show();
    if (app.arguments().contains("--smoke-test")) QTimer::singleShot(150, &app, &QApplication::quit);
    return app.exec();
}
