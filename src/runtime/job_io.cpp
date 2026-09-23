#include "cps/runtime/job_io.hpp"
#include "cps/runtime/shop_io.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>
#include <cstdio>

namespace cps {
namespace {
bool validInteger(const QJsonValue& value, int min, int max) {
    const double number = value.toDouble(-1);
    return value.isDouble() && number >= min && number <= max &&
           std::floor(number) == number;
}
}

bool readJob(const QString& path, Job& job, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) {
        error = QStringLiteral("Cannot open job file or file exceeds 64 KiB");
        return false;
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    const auto object = doc.object();
    const QString id = object.value("job_id").toString();
    const QString title = object.value("title").toString();
    static const QRegularExpression safeId(QStringLiteral("^[A-Za-z0-9_-]{1,80}$"));
    if (parseError.error != QJsonParseError::NoError || !doc.isObject() ||
        object.value("schema_version").toDouble() != 1 ||
        !safeId.match(id).hasMatch() || title.trimmed().isEmpty() || title.size() > 200 ||
        !validInteger(object.value("pages"), 1, 1000) ||
        !validInteger(object.value("copies"), 1, 100) ||
        object.value("pages").toInt() * object.value("copies").toInt() > 10000) {
        error = QStringLiteral("Invalid v1 job: job_id/title/pages/copies");
        return false;
    }
    job.id = id.toStdString();
    job.title = title.toUtf8().toStdString();
    job.pages = object.value("pages").toInt();
    job.copies = object.value("copies").toInt();
    job.state = JobState::Queued;
    if (!orderTermsFromJson(object, job)) { error = "Invalid order specifications"; return false; }
    return true;
}

bool writeJob(const QString& path, const Job& job, QString& error) {
    const QJsonObject object{{"schema_version", 1},
        {"job_id", QString::fromStdString(job.id)},
        {"title", QString::fromUtf8(job.title.data(), static_cast<qsizetype>(job.title.size()))},
        {"pages", job.pages}, {"copies", job.copies}, {"paper", job.paper == PaperSize::A3 ? "a3" : "a4"},
        {"color", job.color}, {"duplex", job.duplex}};
    const auto data = QJsonDocument(object).toJson();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = file.errorString();
        return false;
    }
    return true;
}

void emitEvent(const QJsonObject& event) {
    const auto data = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(data.constData(), 1, static_cast<std::size_t>(data.size()), stdout);
    std::fflush(stdout);
}

QString siblingExecutable(const QString& name) {
#ifdef Q_OS_WIN
    return QDir(QCoreApplication::applicationDirPath()).filePath(name + ".exe");
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(name);
#endif
}

QString defaultStateDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}
} // namespace cps
