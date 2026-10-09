#include "office_preview.hpp"
#include "cps/platform/process.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUrl>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

namespace cps::gui {
OfficePreview::OfficePreview(QObject *parent) : QObject(parent) {
    process_.setProcessChannelMode(QProcess::SeparateChannels);
    // Do not retain a converter's output for arbitrarily large documents.
    process_.setStandardOutputFile(QProcess::nullDevice());
    process_.setStandardErrorFile(QProcess::nullDevice());
    cps::hideProcessConsole(process_);
#if !defined(Q_OS_WIN) && QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    process_.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
#elif !defined(Q_OS_WIN)
    process_.setChildProcessModifier([] { if (::setsid() < 0) ::_exit(126); });
#endif
    deadline_.setSingleShot(true);
    deadline_.setInterval(90000);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        cancel();
        emit failed(QStringLiteral("文档转换超时，请检查文件是否损坏或受密码保护，或先导出 PDF。"));
    });
    connect(&process_, &QProcess::finished, this, &OfficePreview::finish);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (!busy_ || error != QProcess::FailedToStart) return;
        cancel();
        emit failed(QStringLiteral("Office 预览组件无法启动，请检查 LibreOffice 安装或配置路径。"));
    });
#ifdef Q_OS_WIN
    connect(&process_, &QProcess::started, this, [this] {
        // An isolated job owns only this converter and its descendants. Closing
        // it never touches an already open Word/PowerPoint/LibreOffice window.
        auto job = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        auto handle = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                  static_cast<DWORD>(process_.processId()));
        if (job && handle && SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                &limits, sizeof(limits)) && AssignProcessToJobObject(job, handle)) {
            jobHandle_ = job;
        } else {
            if (job) CloseHandle(job);
            // Refuse an unowned conversion instead of risking orphan children.
            cancel();
            emit failed(QStringLiteral("无法建立独立的文档转换进程，请检查系统进程限制。"));
        }
        if (handle) CloseHandle(handle);
    });
#endif
}
OfficePreview::~OfficePreview() { cancel(); }
bool OfficePreview::supports(const QString &path) {
    return QStringList{"doc", "docx", "ppt", "pptx", "xls", "xlsx"}
        .contains(QFileInfo(path).suffix().toLower());
}
QString OfficePreview::executable() {
    // An explicit override is authoritative, including an invalid path. This
    // makes deployment mistakes visible and allows deterministic missing tests.
    if (qEnvironmentVariableIsSet("CPS_OFFICE_EXECUTABLE")) {
        const QFileInfo info(qEnvironmentVariable("CPS_OFFICE_EXECUTABLE"));
        return info.isAbsolute() && info.isFile() && info.isExecutable() ? info.absoluteFilePath() : QString();
    }
    const auto app = QCoreApplication::applicationDirPath();
    QStringList candidates;
#ifdef Q_OS_WIN
    for (const auto &root : {QDir(app).filePath("office"), qEnvironmentVariable("ProgramW6432") + "/LibreOffice",
                            qEnvironmentVariable("ProgramFiles") + "/LibreOffice",
                            qEnvironmentVariable("ProgramFiles(x86)") + "/LibreOffice"}) {
        candidates << QDir(root).filePath("program/soffice.com") << QDir(root).filePath("program/soffice.exe");
    }
    candidates << QStandardPaths::findExecutable("soffice.com") << QStandardPaths::findExecutable("soffice.exe");
#elif defined(Q_OS_MACOS)
    candidates << QDir(app).filePath("../../../office/LibreOffice.app/Contents/MacOS/soffice")
               << QDir(app).filePath("office/LibreOffice.app/Contents/MacOS/soffice")
               << "/Applications/LibreOffice.app/Contents/MacOS/soffice"
               << QDir::home().filePath("Applications/LibreOffice.app/Contents/MacOS/soffice");
#else
    candidates << QDir(app).filePath("office/program/soffice");
#endif
    candidates << QStandardPaths::findExecutable("libreoffice") << QStandardPaths::findExecutable("soffice");
    for (const auto &candidate : candidates) {
        if (candidate.isEmpty()) continue;
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) return info.absoluteFilePath();
    }
    return {};
}
QString OfficePreview::temporaryPath() const { return temporary_ ? temporary_->path() : QString(); }
void OfficePreview::failLater(const QString &message) {
    const auto generation = generation_;
    QTimer::singleShot(0, this, [this, generation, message] {
        if (generation == generation_) emit failed(message);
    });
}
void OfficePreview::start(const QString &path) {
    cancel();
    if (!supports(path)) { failLater(QStringLiteral("此文件不是支持的 Office 文档。")); return; }
    const QFileInfo source(path);
    if (!source.isFile() || source.size() <= 0 || source.size() > 64 * 1024 * 1024) {
        failLater(QStringLiteral("文件无法读取或超过 64 MB。")); return;
    }
    QFile inputCheck(path);
    if (!inputCheck.open(QIODevice::ReadOnly)) { failLater(QStringLiteral("文件无法读取。")); return; }
    const auto header = inputCheck.read(8);
    inputCheck.close();
    const auto suffix = source.suffix().toLower();
    const bool compound = header.startsWith(QByteArray::fromHex("d0cf11e0a1b11ae1"));
    const bool ooxml = header.startsWith(QByteArray::fromHex("504b0304"));
    if (!compound && !(suffix.endsWith("x") && ooxml) && !(suffix == "doc" && header.startsWith("{\\rtf"))) {
        failLater(QStringLiteral("文档格式与扩展名不符，文件可能已损坏；请重新保存或导出 PDF。")); return;
    }
    const auto engine = executable();
    if (engine.isEmpty()) {
        failLater(QStringLiteral("尚未安装 Office 预览组件。请安装 LibreOffice 后重试，或先将文件导出为 PDF。"));
        return;
    }
    temporary_ = std::make_unique<QTemporaryDir>(QDir::temp().filePath("paperroom-office-XXXXXX"));
    if (!temporary_->isValid()) { cancel(); failLater(QStringLiteral("无法创建文档预览的临时目录。")); return; }
    const auto root = temporary_->path();
    // A private copy gives a stable snapshot and never saves over the original.
    const auto input = QDir(root).filePath("document." + source.suffix().toLower());
    const auto output = QDir(root).filePath("output");
    const auto profile = QDir(root).filePath("profile");
    if (!QDir().mkpath(output) || !QDir().mkpath(profile + "/user") || !QFile::copy(path, input)) {
        cancel(); failLater(QStringLiteral("无法读取文件或创建本地转换副本。")); return;
    }
    QFile config(QDir(profile).filePath("user/registrymodifications.xcu"));
    const QByteArray settings = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<oor:items xmlns:oor=\"http://openoffice.org/2001/registry\">"
        "<item oor:path=\"/org.openoffice.Office.Common/Security/Scripting\">"
        "<prop oor:name=\"MacroSecurityLevel\" oor:op=\"fuse\"><value>3</value></prop></item></oor:items>";
    if (!config.open(QIODevice::WriteOnly) || config.write(settings) != settings.size()) {
        config.close(); cancel(); failLater(QStringLiteral("无法初始化独立的文档预览配置。")); return;
    }
    config.close();
    QString filter = "writer_pdf_Export";
    if (source.suffix().startsWith("ppt", Qt::CaseInsensitive)) filter = "impress_pdf_Export";
    else if (source.suffix().startsWith("xls", Qt::CaseInsensitive)) filter = "calc_pdf_Export";
    // In particular, do not enable Calc SinglePageSheets: retain print areas,
    // page breaks, scale and paper settings from the workbook.
    pdfPath_ = QDir(output).filePath("document.pdf");
    process_.setProgram(engine);
    auto environment = QProcessEnvironment::systemEnvironment();
    // LibreOffice's embedded Python otherwise rewrites signed stdlib pyc files
    // after relocation on macOS. Keep the vendor application immutable.
    environment.insert("PYTHONDONTWRITEBYTECODE", "1");
    process_.setProcessEnvironment(environment);
    process_.setWorkingDirectory(root);
    process_.setArguments({"-env:UserInstallation=" + QUrl::fromLocalFile(profile).toString(QUrl::FullyEncoded),
        "--headless", "--nologo", "--nodefault", "--norestore", "--convert-to", "pdf:" + filter,
        "--outdir", output, input});
    busy_ = true;
    deadline_.start();
    process_.start();
}
void OfficePreview::stopProcess() {
#ifdef Q_OS_WIN
    if (jobHandle_) { CloseHandle(static_cast<HANDLE>(jobHandle_)); jobHandle_ = nullptr; }
#else
    if (process_.state() != QProcess::NotRunning && process_.processId() > 0)
        ::kill(-static_cast<pid_t>(process_.processId()), SIGKILL);
#endif
    if (process_.state() != QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(1500);
    }
}
void OfficePreview::cancel() {
    ++generation_;
    busy_ = false;
    deadline_.stop();
    stopProcess();
    temporary_.reset();
    pdfPath_.clear();
}
void OfficePreview::finish(int code, QProcess::ExitStatus status) {
    if (!busy_) return;
    busy_ = false;
    deadline_.stop();
    stopProcess();
    QFile pdf(pdfPath_);
    if (status != QProcess::NormalExit || code != 0 || !pdf.open(QIODevice::ReadOnly) ||
        pdf.size() <= 0 || pdf.size() > 64 * 1024 * 1024 || !pdf.read(4).startsWith("%PDF")) {
        pdf.close(); cancel();
        emit failed(QStringLiteral("Office 文档无法转换，请检查文件是否损坏、受密码保护，或先导出 PDF。"));
        return;
    }
    pdf.close();
    emit ready(pdfPath_);
    // The result stays alive until replacement/removal. PDFium then owns its
    // own source bytes, not a handle into this temporary file.
}
} // namespace cps::gui
