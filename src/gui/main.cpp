#include "print_window.hpp"
#include "cps/runtime/job_io.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>
#include <cstdio>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName("CampusPrint");
    QApplication::setApplicationName("CampusPrintScheduler");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"data-dir", "Store all job/output files under this directory", "directory"},
        {"page-ms", "Simulation delay per page (10..10000)", "ms", "150"},
        {"demo-mode", "Show availability of demonstration controls"},
        {"demo-workload", "Submit five simulated jobs on startup (requires --demo-mode)"},
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
    const auto directory = parser.isSet("data-dir") ? QDir(parser.value("data-dir")).absolutePath() : cps::defaultStateDirectory();
    cps::gui::PrintWindow window({directory, pageMs, parser.isSet("demo-mode")});
    window.show();
    if (parser.isSet("demo-workload")) QTimer::singleShot(250, &window, &cps::gui::PrintWindow::loadDemo);
    if (parser.isSet("smoke-test")) QTimer::singleShot(150, &window, &QWidget::close);
    return app.exec();
}
