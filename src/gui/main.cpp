#include "print_window.hpp"
#include "merchant_window.hpp"
#include "merchant_shell.hpp"
#include "cps/runtime/job_io.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>
#include <cstdio>
#include <memory>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    cps::gui::configureMerchantProcess();
    QApplication::setOrganizationName("CampusPrint");
    QApplication::setApplicationName("CampusPrintScheduler");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"data-dir", "Store all job/output files under this directory", "directory"},
        {"role", "Interface to open: boss (students use the web client)", "role", "boss"},
        {"page-ms", "Simulation delay per page (10..10000)", "ms", "150"},
        {"legacy-mode", "Use the original homogeneous queue for compatibility"},
        {"demo-mode", "Show availability of demonstration controls"},
        {"demo-workload", "Submit mixed demonstration orders on startup (requires --demo-mode)"},
        {"classic-owner", "Open the previous owner console"},
        {"merchant-showcase", "Open interactive owner sample data, without a real backend"},
        {"merchant-capture", "Save owner and scheduling screens for visual review", "directory"},
        {"dispatch-scenario", "Open scheduling workspace scenario (0..7, 7 is live classroom)", "index"},
        {"smoke-test", "Start UI and exit without submitting jobs"}});
    parser.process(app);
    bool validScenario = true;
    const int dispatchScenario = parser.isSet("dispatch-scenario") ? parser.value("dispatch-scenario").toInt(&validScenario) : -1;
    if (!validScenario || dispatchScenario < -1 || dispatchScenario > 7 || (dispatchScenario == 7 && parser.isSet("merchant-showcase")) ||
        (parser.isSet("dispatch-scenario") && (dispatchScenario < 0 || parser.isSet("classic-owner") || parser.isSet("legacy-mode") || parser.isSet("demo-workload")))) {
        std::fprintf(stderr, "--dispatch-scenario requires merchant mode and an index from 0 to 7; live scenario 7 requires a real backend\n");
        return 64;
    }
    const auto role = parser.value("role");
    if (role != "boss") {
        std::fprintf(stderr, "--role must be boss; students use tools/classroom_web.py\n");
        return 64;
    }
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
    const cps::gui::WindowOptions options{directory, pageMs, parser.isSet("demo-mode"), !parser.isSet("legacy-mode")};
    // The owner console and web gateway share the coordinator's state directory.
    std::unique_ptr<QMainWindow> window;
    if (!parser.isSet("classic-owner") && !parser.isSet("legacy-mode") && !parser.isSet("demo-workload")) {
        auto* console = new cps::gui::MerchantWindow(options, parser.isSet("merchant-showcase"));
        window.reset(console);
        if (dispatchScenario >= 0) console->openDispatchWorkspace(dispatchScenario);
        if (parser.isSet("merchant-capture")) {
            QObject::connect(console, &cps::gui::MerchantWindow::backendReady, console, [console, &app, &parser] {
                QTimer::singleShot(350, console, [console, &app, &parser] {console->captureAll(parser.value("merchant-capture")); app.quit();});
            });
        } else if (parser.isSet("smoke-test"))
            QObject::connect(console, &cps::gui::MerchantWindow::backendReady, console, &QWidget::close, Qt::QueuedConnection);
    } else {
        auto* console = new cps::gui::PrintWindow(options);
        window.reset(console);
        if (parser.isSet("demo-workload")) {
            auto submitted = std::make_shared<bool>(false);
            QObject::connect(console, &cps::gui::PrintWindow::backendReady, console, [console, submitted] {
                if (!*submitted) { *submitted = true; console->loadDemo(); }
            });
        }
        if (parser.isSet("smoke-test"))
            QObject::connect(console, &cps::gui::PrintWindow::backendReady, console, &QWidget::close, Qt::QueuedConnection);
    }
    window->show();
    if (parser.isSet("smoke-test"))
        QTimer::singleShot(10000, &app, [&] { if (window->isVisible()) app.exit(69); });
    return app.exec();
}
