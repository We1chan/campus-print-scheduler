#include "print_window.hpp"
#include "cps/runtime/job_io.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>
#include <cstdio>
#include <memory>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("CampusPrint");
    QApplication::setApplicationName("CampusPrintScheduler");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"data-dir", "Store all job/output files under this directory", "directory"},
        {"page-ms", "Simulation delay per page (10..10000)", "ms", "150"},
        {"legacy-mode", "Use the original homogeneous queue for compatibility"},
        {"demo-mode", "Show availability of demonstration controls"},
        {"demo-workload", "Submit mixed demonstration orders on startup (requires --demo-mode)"},
        {"smoke-test", "Start UI and exit without submitting jobs"}});
    parser.process(app);
    if (parser.isSet("demo-workload") && !parser.isSet("demo-mode")) {
        std::fprintf(stderr, "--demo-workload requires --demo-mode\n");
        return 64;
    }
    bool valid = false;
    const int pageMs = parser.value("page-ms").toInt(&valid);
    if (!valid || pageMs < 10 || pageMs > 10000) {
        std::fprintf(stderr, "--page-ms must be an integer in 10..10000\n");
        return 64;
    }
    const auto directory = parser.isSet("data-dir") ? QDir(parser.value("data-dir")).absolutePath() : QDir(cps::defaultStateDirectory()).filePath(parser.isSet("legacy-mode") ? "queue" : "shop");
    cps::gui::PrintWindow window({directory, pageMs, parser.isSet("demo-mode"), !parser.isSet("legacy-mode")});
    window.show();
    if (parser.isSet("demo-workload")) {
        auto submitted = std::make_shared<bool>(false);
        QObject::connect(&window, &cps::gui::PrintWindow::backendReady, &window, [&window, submitted] {
            if (!*submitted) { *submitted = true; window.loadDemo(); }
        });
    }
    if (parser.isSet("smoke-test")) {
        QObject::connect(&window, &cps::gui::PrintWindow::backendReady, &window, &QWidget::close, Qt::QueuedConnection);
        QTimer::singleShot(10000, &app, [&] { if (window.isVisible()) app.exit(69); });
    }
    return app.exec();
}
