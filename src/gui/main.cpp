#include "print_window.hpp"
#include "student_window.hpp"
#include "merchant_window.hpp"
#include "merchant_shell.hpp"
#include "preview_check.hpp"
#include "editorial_chrome.hpp"
#include "cps/runtime/job_io.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTimer>
#include <cstdio>
#include <memory>

static void initializeBrandResources() { Q_INIT_RESOURCE(editorial_assets); }

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    initializeBrandResources();
    QApplication::setWindowIcon(cps::gui::editorialBrandIcon());
    QApplication::setOrganizationName("CampusPrint");
    QApplication::setApplicationName("CampusPrintScheduler");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOptions({{"data-dir", "Store all job/output files under this directory", "directory"},
        {"role", "Interface to open: boss (owner console) or student (self-service client)", "role", "boss"},
        {"student", "Student ID used by the self-service client", "id"},
        {"document", "Open a local document in the student interface", "path"},
        {"check-preview", "Render a local document for deployment diagnostics without starting the backend", "path"},
        {"preview-report", "JSON result file for --check-preview", "path"},
        {"page-ms", "Simulation delay per page (10..10000)", "ms", "150"},
        {"legacy-mode", "Use the original homogeneous queue for compatibility"},
        {"demo-mode", "Show availability of demonstration controls"},
        {"demo-workload", "Submit mixed demonstration orders on startup (requires --demo-mode)"},
        {"classic-owner", "Open the previous owner console"},
        {"merchant-showcase", "Open interactive owner sample data, without a real backend"},
        {"merchant-capture", "Save all four rendered owner screens for visual review", "directory"},
        {"smoke-test", "Start UI and exit without submitting jobs"}});
    parser.process(app);
    if (parser.isSet("check-preview")) {
        if (!parser.isSet("preview-report")) return 64;
        return cps::gui::checkDocumentPreview(parser.value("check-preview"), parser.value("preview-report"));
    }
    const auto role = parser.value("role");
    const bool student = role == "student";
    if (!student && role != "boss") {
        std::fprintf(stderr, "--role must be boss or student\n");
        return 64;
    }
    if (student && (parser.isSet("legacy-mode") || parser.isSet("demo-workload"))) {
        std::fprintf(stderr, "--role student needs the shop mode and cannot load the demo workload\n");
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
    // Both interfaces are clients of the same coordinator; either one may start it.
    std::unique_ptr<QMainWindow> window;
    if (student) {
        auto* client = new cps::gui::StudentWindow(options, parser.value("student"));
        window.reset(client);
        if (parser.isSet("document"))
            client->setDocument(parser.value("document"));
        if (parser.isSet("smoke-test"))
            QObject::connect(client, &cps::gui::StudentWindow::backendReady, client, &QWidget::close, Qt::QueuedConnection);
    } else if (!parser.isSet("classic-owner") && !parser.isSet("legacy-mode") && !parser.isSet("demo-workload")) {
        cps::gui::configureMerchantProcess();
        auto* console = new cps::gui::MerchantWindow(options, parser.isSet("merchant-showcase"));
        window.reset(console);
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
