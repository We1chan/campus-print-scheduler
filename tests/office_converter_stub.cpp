// Process-boundary test double, never shipped in an application package.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    const auto out = args.indexOf("--outdir");
    if (out < 0 || out + 1 >= args.size()) return 64;
    QFile source(args.last());
    if (!source.open(QIODevice::ReadOnly)) return 65;
    const auto fixture = QJsonDocument::fromJson(source.readAll().mid(4)).object();
    const auto output = QDir(args[out + 1]).filePath("document.pdf");
    const auto finish = [&app, fixture, output] {
        const bool copied = QFile::copy(fixture.value("pdf").toString(), output);
        app.exit(copied ? 0 : 66);
    };
    QTimer gate;
    const auto release = fixture.value("release_file").toString();
    if (release.isEmpty()) QTimer::singleShot(fixture.value("delay_ms").toInt(), &app, finish);
    else {
        // The GUI must process an event before this child can finish. This is
        // independent of timer coalescing or scheduler load on a CI runner.
        QObject::connect(&gate, &QTimer::timeout, &app, [release, finish] {
            if (QFile::exists(release)) finish();
        });
        gate.start(10);
        QTimer::singleShot(5000, &app, [&app] { app.exit(67); });
    }
    return app.exec();
}
