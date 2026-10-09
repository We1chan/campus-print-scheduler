#include "event_stream.hpp"
#include "print_window.hpp"
#include "student_window.hpp"
#include "paper_workspace.hpp"
#include "print_proof.hpp"
#include "payment_receipt.hpp"
#include "office_preview.hpp"
#include "coordinator_test_client.hpp"
#include "native_frame_evidence.hpp"
#include "windows_test_logs.hpp"
#include "cps/runtime/job_io.hpp"
#include <QProcess>
#include <memory>
#include <cmath>

#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include <QTableWidget>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTableView>
#include <QToolButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QDialog>
#include <QListWidget>
#include <QPdfWriter>
#include <QPainter>

using namespace cps::gui;

class TimedPrintProof : public PrintProof {
public:
    bool recording = false;
    QVector<double> paints, intervals;
    QElapsedTimer clock;
    qint64 previous = 0;
protected:
    void paintEvent(QPaintEvent *event) override {
        if (!clock.isValid()) clock.start();
        const auto start = clock.nsecsElapsed();
        PrintProof::paintEvent(event);
        if (!recording) return;
        paints.append((clock.nsecsElapsed() - start) / 1e6);
        if (previous) intervals.append((start - previous) / 1e6);
        previous = start;
    }
};

class ProofFrameIntervals : public QObject {
public:
    QVector<double> intervals;
    bool recording = false;
    QElapsedTimer clock;
    qint64 previous = 0;
    bool eventFilter(QObject *, QEvent *event) override {
        if (recording && event->type() == QEvent::Paint) {
            if (!clock.isValid()) clock.start();
            const auto now = clock.nsecsElapsed();
            if (previous) intervals.append((now - previous) / 1e6);
            previous = now;
        }
        return false;
    }
};

class GuiTests : public QObject {
    Q_OBJECT
    static void openForm(PrintWindow& window) {
        window.findChild<QPushButton*>("newTask")->click();
        auto* dialog = window.findChild<QDialog*>("submissionDialog");
        QVERIFY(dialog && dialog->isVisible());
    }
private slots:
    void initTestCase() {
        const auto error = isolateTestReportHandles();
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
    void officeConversionLifecycle() {
        const auto previous = qgetenv("CPS_OFFICE_EXECUTABLE");
        const bool wasSet = qEnvironmentVariableIsSet("CPS_OFFICE_EXECUTABLE");
        struct Restore {
            QByteArray previous; bool wasSet;
            ~Restore() { if (wasSet) qputenv("CPS_OFFICE_EXECUTABLE", previous); else qunsetenv("CPS_OFFICE_EXECUTABLE"); }
        } restore{previous, wasSet};
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto pdfPath = QDir(root.path()).filePath("source.pdf");
        { QPdfWriter pdf(pdfPath); QPainter painter(&pdf); painter.fillRect(0, 0, pdf.width(), pdf.height(), Qt::red);
          QVERIFY(pdf.newPage()); painter.fillRect(0, 0, pdf.width(), pdf.height(), Qt::blue); }
        const auto officePath = QDir(root.path()).filePath(QStringLiteral("中文 文档.docx"));
        const auto writeFixture = [&](int delay) {
            QFile file(officePath); if (!file.open(QIODevice::WriteOnly)) return false;
            return file.write(QByteArray::fromHex("504b0304") + QJsonDocument(QJsonObject{{"pdf", pdfPath}, {"delay_ms", delay}}).toJson()) > 0;
        };
        QVERIFY(writeFixture(60));
        const auto stub = QDir(QCoreApplication::applicationDirPath()).filePath("office_converter_stub"
#ifdef Q_OS_WIN
            ".exe"
#endif
        );
        qputenv("CPS_OFFICE_EXECUTABLE", stub.toUtf8());
        OfficePreview conversion;
        QSignalSpy ready(&conversion, &OfficePreview::ready), failed(&conversion, &OfficePreview::failed);
        int ticks = 0;
        QTimer responsive; responsive.setInterval(5);
        connect(&responsive, &QTimer::timeout, [&] { ++ticks; }); responsive.start();
        conversion.start(officePath);
        const auto temporary = conversion.temporaryPath();
        QVERIFY(conversion.busy()); QVERIFY(!temporary.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 5000);
        QVERIFY(ticks > 2); QCOMPARE(failed.size(), 0);
        DocumentPreview document; QVERIFY(document.load(ready.at(0).at(0).toString()));
        QCOMPARE(document.pageCount(), 2); QVERIFY(document.page(0) != document.page(1));
        conversion.cancel(); QVERIFY(!QFileInfo::exists(temporary));
        QVERIFY(writeFixture(1000)); conversion.start(officePath);
        const auto stale = conversion.temporaryPath();
        conversion.cancel(); QVERIFY(!QFileInfo::exists(stale));
        QVERIFY(writeFixture(0)); conversion.start(officePath);
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 2, 5000);
        QTest::qWait(1100); QCOMPARE(ready.size(), 2);
        conversion.cancel();
        qputenv("CPS_OFFICE_EXECUTABLE", "missing-converter");
        conversion.start(officePath); QTRY_COMPARE(failed.size(), 1);
        QVERIFY(failed.at(0).at(0).toString().contains(QStringLiteral("LibreOffice")));
        qputenv("CPS_OFFICE_EXECUTABLE", stub.toUtf8());
        { QFile corrupt(officePath); QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("not a document"); }
        conversion.start(officePath); QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 5000);
        QVERIFY(conversion.temporaryPath().isEmpty());
        for (const auto *suffix : {"doc", "docx", "ppt", "pptx", "xls", "xlsx"})
            QVERIFY(OfficePreview::supports(QString("file.") + suffix));
        QVERIFY(!OfficePreview::supports("file.pdf"));
    }
    void officeDocumentsRealPreview_data() {
        QTest::addColumn<QString>("suffix");
        for (const auto *suffix : {"doc", "docx", "ppt", "pptx", "xls", "xlsx"}) QTest::newRow(suffix) << QString(suffix);
    }
    void officeDocumentsRealPreview() {
        const auto fixtures = qEnvironmentVariable("CPS_OFFICE_FIXTURES");
        if (fixtures.isEmpty()) {
            QTest::qSkip("Set CPS_OFFICE_FIXTURES to the generated real Office fixture directory", __FILE__, __LINE__);
            return;
        }
        QFETCH(QString, suffix);
        const auto original = QDir(fixtures).filePath(QStringLiteral("中文 空格.") + suffix);
        QFile source(original); QVERIFY(source.open(QIODevice::ReadOnly)); const auto bytes = source.readAll(); source.close();
        OfficePreview conversion;
        QSignalSpy ready(&conversion, &OfficePreview::ready), failed(&conversion, &OfficePreview::failed);
        conversion.start(original);
        QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty() || !failed.isEmpty(), 100000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.at(0).at(0).toString()));
        QCOMPARE(ready.size(), 1);
        DocumentPreview preview; QVERIFY2(preview.load(ready.at(0).at(0).toString()), qPrintable(preview.error()));
        QCOMPARE(preview.pageCount(), 2);
        const auto shape = preview.pageSize(0);
        if (suffix.startsWith("ppt")) QVERIFY(shape.width() > shape.height());
        else QVERIFY(shape.width() < shape.height());
        QVERIFY(!preview.page(0).isNull()); QVERIFY(!preview.page(1).isNull());
        QVERIFY(preview.page(0) != preview.page(1));
        QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(source.readAll(), bytes);
        const auto temp = conversion.temporaryPath(); conversion.cancel(); QVERIFY(!QFileInfo::exists(temp));
    }
    void officeStudentPreviewAndReplacement() {
        const auto fixtures = qEnvironmentVariable("CPS_OFFICE_FIXTURES");
        if (fixtures.isEmpty()) {
            QTest::qSkip("Real Office fixtures not configured", __FILE__, __LINE__);
            return;
        }
        BackendDirectory root;
        StudentWindow student({root.path(), 20, false, true}); student.resize(1180, 820); student.show();
        auto *proof = dynamic_cast<PrintProof *>(student.findChild<QWidget *>("printProof")); QVERIFY(proof);
        const auto doc = QDir(fixtures).filePath(QStringLiteral("中文 空格.docx"));
        student.setDocument(doc);
        QVERIFY(!student.findChild<QPushButton *>("payOrder")->isEnabled());
        QTRY_COMPARE_WITH_TIMEOUT(proof->documentPages(), 2, 100000);
        QCOMPARE(student.findChild<QSpinBox *>("orderPages")->value(), 2);
        QVERIFY(proof->currentPaperSize().width() < proof->currentPaperSize().height());
        QVERIFY(!student.findChild<QSpinBox *>("orderPages")->isEnabled());
        proof->openReader(); auto *reader = student.findChild<QDialog *>("documentReader"); QVERIFY(reader);
        QVERIFY(!reader->findChild<QLabel *>("readerImage")->pixmap().isNull());
        reader->findChild<QPushButton *>("readerNext")->click();
        QVERIFY(reader->findChild<QLabel *>("readerPageCount")->text().contains("2 / 2"));
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) { QDir().mkpath(evidence); QVERIFY(reader->grab().save(QDir(evidence).filePath("office-word-reader.png"))); }
        student.setDocument(QDir(fixtures).filePath(QStringLiteral("中文 空格.pptx")));
        QVERIFY(!reader->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(proof->documentPages(), 2, 100000);
        QVERIFY(proof->currentPaperSize().width() > proof->currentPaperSize().height());
        const auto settings = proof->specification();
        auto *rotate = student.findChild<QPushButton *>("rotateProof"); QVERIFY(rotate && rotate->isEnabled());
        rotate->click(); QCOMPARE(proof->previewRotation(), 90);
        QVERIFY(proof->currentPaperSize().width() < proof->currentPaperSize().height());
        QCOMPARE(proof->specification(), settings); QCOMPARE(proof->documentPages(), 2);
        for (int i = 0; i < 3; ++i) rotate->click();
        QCOMPARE(proof->previewRotation(), 0);
        proof->openReader(); auto *slideReader = student.findChild<QDialog *>("documentReader"); QVERIFY(slideReader);
        auto *slideImage = slideReader->findChild<QLabel *>("readerImage"); QVERIFY(slideImage);
        QVERIFY(slideImage->pixmap().width() > slideImage->pixmap().height());
        const auto page = proof->renderedPage(0);
        QVERIFY(std::abs(double(page.width()) / page.height() - double(slideImage->pixmap().width()) / slideImage->pixmap().height()) < .02);
        if (!evidence.isEmpty()) {
            QTest::qWait(120);
            QVERIFY(slideReader->grab().save(QDir(evidence).filePath("office-ppt-reader.png")));
            slideReader->close(); QTest::qWait(50);
            QVERIFY(student.grab().save(QDir(evidence).filePath("office-ppt-desk.png")));
        }
        student.setDocument(QDir(fixtures).filePath(QStringLiteral("中文 空格.pptx")));
        student.findChild<QPushButton *>("removeFile")->click();
        QTest::qWait(200); QCOMPARE(proof->documentPages(), 0);
        QVERIFY(student.findChild<QSpinBox *>("orderPages")->isEnabled());
        student.setDocument(doc);
        student.setDocument(QDir(fixtures).filePath(QStringLiteral("中文 空格.xlsx")));
        QTRY_COMPARE_WITH_TIMEOUT(proof->documentPages(), 2, 100000);
        QVERIFY(student.findChild<QLabel *>("fileName")->toolTip().endsWith(".xlsx"));
        student.close(); QTRY_VERIFY(!student.isVisible());
    }
    void previewPreservesMixedPageDirections() {
        QTemporaryDir root; QVERIFY(root.isValid());
        const auto path = QDir(root.path()).filePath("mixed.pdf");
        { QPdfWriter pdf(path); pdf.setPageSize(QPageSize(QPageSize::A4));
          QPainter painter(&pdf); painter.fillRect(0, 0, pdf.width(), pdf.height(), Qt::red);
          pdf.setPageOrientation(QPageLayout::Landscape); QVERIFY(pdf.newPage());
          painter.fillRect(0, 0, pdf.width(), pdf.height(), Qt::blue); }
        PrintProof proof; proof.resize(1100, 650); proof.setMotion(false);
        proof.setSpecification("Mixed", 2, 1, false, true, false, true);
        QVERIFY(proof.loadDocument(path)); proof.show();
        QVERIFY(proof.currentPaperSize().width() < proof.currentPaperSize().height());
        proof.stepPage(1);
        QVERIFY(proof.currentPaperSize().width() > proof.currentPaperSize().height());
        const auto original = proof.renderedPage(1);
        proof.rotatePreview(); QVERIFY(proof.currentPaperSize().width() < proof.currentPaperSize().height());
        for (int i = 0; i < 3; ++i) proof.rotatePreview();
        QCOMPARE(proof.renderedPage(1), original);
        proof.openReader(); auto *reader = proof.findChild<QDialog *>("documentReader"); QVERIFY(reader);
        auto *image = reader->findChild<QLabel *>("readerImage"); QVERIFY(image);
        QVERIFY(image->pixmap().width() > image->pixmap().height());
        const auto ratio = double(image->pixmap().width()) / image->pixmap().height();
        reader->resize(800, 700); QTest::qWait(60);
        QVERIFY(std::abs(double(image->pixmap().width()) / image->pixmap().height() - ratio) < .01);
        reader->findChild<QPushButton *>("readerPrevious")->click();
        QVERIFY(image->pixmap().width() < image->pixmap().height());
        proof.clearDocument(); QVERIFY(!reader->isVisible());
    }
    void editorialMotionRetargeting() {
        PaperTransition motion(nullptr);
        motion.retarget(0., 1.);
        motion.setCurrentTime(80);
        const qreal position = motion.currentValue().toReal(), velocity = motion.velocity();
        QVERIFY(position > 0. && position < 1. && velocity > 0.);
        motion.retarget(position, 0.);
        QCOMPARE(motion.currentValue().toReal(), position);
        QVERIFY(std::abs(motion.velocity() - velocity) < 1e-6);
        motion.setCurrentTime(16);
        QVERIFY(motion.currentValue().toReal() >= position); // decelerate before reversing
        for (int t = 17; t <= motion.duration(); ++t) {
            motion.setCurrentTime(t);
            QVERIFY(motion.currentValue().toReal() >= 0. && motion.currentValue().toReal() <= 1.);
        }
        QCOMPARE(motion.currentValue().toReal(), 0.);
        QCOMPARE(motion.velocity(), 0.);
    }
    void editorialAnimationFrameBudget() {
        BackendDirectory root; QVERIFY(root.isValid());
        StudentWindow resources({root.path(), 20, false, true});
        TimedPrintProof proof;
        // This benchmark drives the animation explicitly; the user's real
        // pointer must not retarget it while the test window appears.
        proof.setAttribute(Qt::WA_Hover, false);
        proof.resize(1218, 788); proof.show();
        QVERIFY(QTest::qWaitForWindowExposed(&proof));
        // Measure a real event loop, rather than QtTest's intermittent polling
        // sleeps, which can themselves postpone animation timer delivery.
        const auto transition = [](PrintProof &scene, bool open) {
            QEventLoop loop;
            QTimer watchdog;
            watchdog.setSingleShot(true);
            QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
            auto *animation = scene.findChildren<QVariantAnimation *>().front();
            QObject::connect(animation, &QVariantAnimation::finished, &loop, &QEventLoop::quit);
            scene.setExpanded(open);
            watchdog.start(3000);
            loop.exec();
            QCOMPARE(scene.expansionProgress(), open ? 1. : 0.);
        };
        proof.setExpanded(true); QTRY_COMPARE(proof.expansionProgress(), 1.);
        proof.setExpanded(false); QTRY_COMPARE(proof.expansionProgress(), 0.);
        // Native CI displays can coalesce several animation updates into a
        // single paint. Collect enough real samples, with a bounded retry.
        for (int round = 0; round < 12 && (round < 3 || proof.paints.size() < 12 || proof.intervals.size() < 12); ++round) {
            proof.previous = 0; proof.recording = true;
            transition(proof, true);
            proof.recording = false;
            proof.previous = 0; proof.recording = true;
            transition(proof, false);
            proof.recording = false;
        }
        QVERIFY(proof.paints.size() >= 12);
        QVERIFY(proof.intervals.size() >= 12);
        const auto percentile = [](QVector<double> values, double ratio) {
            std::sort(values.begin(), values.end());
            return values[qMin(values.size() - 1, qsizetype(values.size() * ratio))];
        };
        qInfo().noquote() << QStringLiteral("Animation: %1 frames, paint p50=%2ms p95=%3ms, frame interval p95=%4ms")
            .arg(proof.paints.size()).arg(percentile(proof.paints, .5), 0, 'f', 2)
            .arg(percentile(proof.paints, .95), 0, 'f', 2).arg(percentile(proof.intervals, .95), 0, 'f', 2);
        const bool enforceBudget = qEnvironmentVariableIsSet("CPS_GUI_VERIFY_FRAME_BUDGET");
        if (enforceBudget) {
            QVERIFY2(percentile(proof.paints, .95) < 16.7, "Paper painting exceeds a 60Hz frame budget");
            QVERIFY2(percentile(proof.intervals, .95) < 27., "Animation frames have visible stalls");
        }
        proof.close();
        resources.resize(1420, 820); resources.show();
        QVERIFY(QTest::qWaitForWindowExposed(&resources));
        auto *scene = dynamic_cast<PrintProof *>(resources.findChild<QWidget *>("printProof"));
        QVERIFY(scene);
        scene->setAttribute(Qt::WA_Hover, false);
        ProofFrameIntervals frames;
        scene->installEventFilter(&frames);
        const auto measureScene = [&](const QString &name) {
            transition(*scene, true); transition(*scene, false); // warm the visible pages
            frames.intervals.clear();
            for (int round = 0; round < 12 && (round < 3 || frames.intervals.size() < 12); ++round) {
                for (bool open : {true, false}) {
                    frames.previous = 0; frames.recording = true;
                    transition(*scene, open);
                    frames.recording = false;
                }
            }
            QVERIFY(frames.intervals.size() >= 12);
            qInfo().noquote() << name << "full-window frame intervals=" << frames.intervals.size()
                             << "p95=" << percentile(frames.intervals, .95);
            if (enforceBudget) QVERIFY2(percentile(frames.intervals, .95) < 27., "Full application has animation stalls");
        };
        measureScene("Sample");
        const QString path = QDir(root.path()).filePath("motion.pdf");
        {
            QPdfWriter pdf(path); pdf.setResolution(96);
            QPainter p(&pdf);
            for (int page = 0; page < 4; ++page) {
                if (page) QVERIFY(pdf.newPage());
                p.fillRect(QRect(0, 0, 800, 1100), QColor::fromHsv(page * 75, 160, 200));
                p.drawText(QRect(20, 20, 750, 100), Qt::AlignCenter, "PDF animation test");
            }
        }
        resources.setDocument(path);
        resources.findChild<QCheckBox *>("orderColor")->setChecked(false);
        measureScene("Grayscale PDF");
        resources.close();
    }
    void editorialReceiptCodes() {
        QTemporaryDir root; QVERIFY(root.isValid());
        Q_INIT_RESOURCE(editorial_assets);
        ReceiptCodePanel defaults(root.path(), true);
        auto *defaultCode = defaults.findChild<QLabel *>("paymentReceiptCode");
        for (const QString &method : {QString("wechat"), QString("alipay")}) {
            defaults.setMethod(method);
            QVERIFY(defaultCode->property("configured").toBool());
            QCOMPARE(defaultCode->property("sourcePath").toString(), ":/editorial/payment-codes/" + method + ".jpg");
        }
        ReceiptCodePanel panel(root.path());
        panel.setMethod("wechat");
        auto *code = panel.findChild<QLabel *>("paymentReceiptCode");
        auto *title = panel.findChild<QLabel *>("paymentReceiptTitle");
        QVERIFY(code && title);
        QVERIFY(!code->property("configured").toBool());
        QCOMPARE(title->text(), QStringLiteral("微信收款码"));
        QImage wechat(80, 120, QImage::Format_RGB32); wechat.fill(Qt::green);
        QImage alipay(120, 80, QImage::Format_RGB32); alipay.fill(Qt::blue);
        QVERIFY(wechat.save(root.filePath("wechat.png")));
        QVERIFY(alipay.save(root.filePath("alipay.png")));
        defaults.setMethod("wechat");
        QCOMPARE(defaultCode->property("sourcePath").toString(), root.filePath("wechat.png"));
        panel.setMethod("wechat");
        QVERIFY(code->property("configured").toBool());
        QCOMPARE(code->pixmap().toImage().pixelColor(20, 20), QColor(Qt::green));
        QVERIFY(code->pixmap().height() > code->pixmap().width()); // preserve the full image
        panel.setMethod("alipay");
        QCOMPARE(title->text(), QStringLiteral("支付宝收款码"));
        QCOMPARE(code->pixmap().toImage().pixelColor(20, 20), QColor(Qt::blue));
        QVERIFY(code->pixmap().width() > code->pixmap().height());
        panel.setMethod("campus_card");
        QVERIFY(!code->property("configured").toBool());
        QVERIFY(code->property("sourcePath").toString().isEmpty());
        QCOMPARE(title->text(), QStringLiteral("校园卡支付"));
        { QFile broken(root.filePath("wechat.png")); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("invalid image"); }
        defaults.setMethod("wechat");
        QVERIFY(!defaultCode->property("configured").toBool());
        panel.setMethod("wechat");
        QVERIFY(!code->property("configured").toBool()); // never leave another provider's image on screen
        QVERIFY(QFile::remove(root.filePath("wechat.png")));
        QVERIFY(wechat.save(root.filePath("wechat.jpg")));
        panel.setMethod("wechat");
        QVERIFY(code->property("configured").toBool());
        QVERIFY(code->property("sourcePath").toString().endsWith("wechat.jpg"));
        panel.show();
        panel.findChild<QPushButton *>("paymentReceiptEnlarge")->click();
        auto *enlarged = panel.findChild<QDialog *>("receiptCodeDialog");
        QVERIFY(enlarged && enlarged->isVisible());
        QVERIFY(enlarged->windowFlags().testFlag(Qt::FramelessWindowHint));
        auto *full = enlarged->findChild<QLabel *>("receiptCodeFullImage");
        QVERIFY(full && !full->pixmap().isNull());
        QVERIFY(full->pixmap().height() > full->pixmap().width());
        enlarged->close();
        const QString actualDirectory = qEnvironmentVariable("CPS_GUI_RECEIPT_DIRECTORY");
        if (!actualDirectory.isEmpty()) {
            ReceiptCodePanel actual(actualDirectory);
            actual.resize(540, 190); actual.show();
            for (const QString &method : {QString("wechat"), QString("alipay")}) {
                actual.setMethod(method);
                QVERIFY(actual.findChild<QLabel *>("paymentReceiptCode")->property("configured").toBool());
                actual.findChild<QPushButton *>("paymentReceiptEnlarge")->click();
                auto *preview = actual.findChild<QDialog *>("receiptCodeDialog");
                QVERIFY(preview && preview->isVisible());
                QVERIFY(preview->findChild<QLabel *>("receiptCodeFullImage")->pixmap().height() > 400);
                const QString evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
                if (!evidence.isEmpty()) {
                    QVERIFY(QDir().mkpath(evidence));
                    QVERIFY(preview->grab().save(QDir(evidence).filePath(method + "-receipt-native.png")));
                }
                preview->close();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
        }
    }
    void editorialHoverSpreadsAndReturns() {
        BackendDirectory root; QVERIFY(root.isValid());
        StudentWindow student({root.path(), 20, false, true});
        student.resize(1420, 888); student.show();
        QVERIFY(QTest::qWaitForWindowExposed(&student));
        student.raise(); student.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&student));
        auto *proof = dynamic_cast<PrintProof*>(student.findChild<QWidget*>("printProof"));
        auto *pin = student.findChild<QPushButton*>("unfoldProof");
        auto *motion = student.findChild<QPushButton*>("studentReduceMotion");
        QVERIFY(proof && pin && motion);
        const QPoint title(qRound(proof->width() * 40. / 1218), qRound(proof->height() * 60. / 788));
        const QPoint paper(proof->width() / 2, proof->height() / 2);
        // Inject through the window-system event path instead of moving the
        // shared desktop cursor; the app reads the event's global position.
        const auto moveMouse = [&student](QWidget *widget, QPoint point) {
            QTest::mouseMove(student.windowHandle(), widget->mapTo(&student, point));
        };
        moveMouse(proof, title); QTest::qWait(500);
        QVERIFY(!proof->hoverExpanded() && !proof->expanded());
        const auto closed = proof->grab().toImage();
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) { QVERIFY(QDir().mkpath(evidence)); QVERIFY(proof->grab().save(QDir(evidence).filePath("hover-closed.png"))); }
        moveMouse(proof, paper);
        QTRY_VERIFY(proof->hoverExpanded());
        QVERIFY(!pin->isChecked()); // hovering does not latch the manual control
        QTRY_VERIFY(proof->expansionProgress() > 0.);
        QVERIFY(proof->expansionProgress() < 1.);
        const auto middle = proof->grab().toImage();
        QVERIFY(middle != closed);
        if (!evidence.isEmpty()) QVERIFY(proof->grab().save(QDir(evidence).filePath("hover-moving.png")));
        QTRY_COMPARE(proof->expansionProgress(), 1.);
        const auto opened = proof->grab().toImage();
        QVERIFY(opened != closed && opened != middle);
        if (!evidence.isEmpty()) QVERIFY(proof->grab().save(QDir(evidence).filePath("hover-open.png")));
        moveMouse(pin, pin->rect().center());
        QTRY_VERIFY(proof->hoverExpanded()); // tools over the paper do not collapse it
        moveMouse(proof, title);
        QTRY_VERIFY(!proof->hoverExpanded());
        QTRY_COMPARE(proof->expansionProgress(), 0.);
        QTRY_COMPARE(proof->grab().toImage(), closed);
        // Reversing before the opening finishes returns to the same resting pose.
        moveMouse(proof, paper); QTest::qWait(40);
        moveMouse(proof, title);
        QTRY_COMPARE(proof->expansionProgress(), 0.);
        QTRY_COMPARE(proof->grab().toImage(), closed);
        pin->click(); QTRY_COMPARE(proof->expansionProgress(), 1.);
        moveMouse(proof, paper); moveMouse(proof, title);
        QVERIFY(proof->expanded() && pin->isChecked());
        QVERIFY(proof->grab().toImage() != closed);
        pin->click();
        QVERIFY(!proof->expanded() && !proof->hoverExpanded());
        QTRY_COMPARE(proof->expansionProgress(), 0.);
        if (!evidence.isEmpty()) QVERIFY(proof->grab().save(QDir(evidence).filePath("hover-returned.png")));
        QCOMPARE(proof->grab().toImage(), closed);
        moveMouse(proof, paper); QTRY_VERIFY(proof->hoverExpanded());
        motion->click();
        QVERIFY(!proof->motionEnabled() && !proof->hoverExpanded());
        moveMouse(proof, title); moveMouse(proof, paper);
        QVERIFY(!proof->hoverExpanded());
        QCOMPARE(proof->grab().toImage(), closed);
        pin->click(); QVERIFY(proof->expanded()); // reduced motion keeps explicit controls usable
        QVERIFY(proof->grab().toImage() != closed);
        student.close();
    }
    void editorialWindowChrome() {
        BackendDirectory root; QVERIFY(root.isValid());
        StudentWindow student({root.path(), 20, false, true});
        student.resize(1180, 820); student.show();
        QVERIFY(QTest::qWaitForWindowExposed(&student));
        QVERIFY(student.windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(!student.windowIcon().isNull());
        const auto frameError=verifyNativeFrame(student,qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR"),"student-native-frame.png");
        QVERIFY2(frameError.isEmpty(),qPrintable(frameError));
        auto *maximize = student.findChild<QPushButton*>("windowMaximize");
        auto *minimize = student.findChild<QPushButton*>("windowMinimize");
        auto *close = student.findChild<QPushButton*>("windowClose");
        QVERIFY(maximize && minimize && close);
        const QRect original = student.geometry();
        QTest::mouseClick(maximize, Qt::LeftButton); QTRY_VERIFY(student.isMaximized());
        QTest::mouseClick(maximize, Qt::LeftButton); QTRY_VERIFY(!student.isMaximized());
        QTRY_COMPARE(student.size(), original.size());
        QTest::mouseClick(minimize, Qt::LeftButton); QTRY_VERIFY(student.isMinimized());
        student.showNormal();
        student.findChild<QPushButton*>("navMine")->click();
        QVERIFY(student.findChild<QPushButton*>("navMine")->isChecked());
        student.findChild<QPushButton*>("navOrder")->click();
        QTest::mouseClick(close, Qt::LeftButton); QTRY_VERIFY(!student.isVisible());
    }
    void editorialDocumentPreview() {
        BackendDirectory root; QVERIFY(root.isValid());
        const QString pdfPath=QDir(root.path()).filePath(QStringLiteral("中文 文件 预览.pdf"));
        {
            QPdfWriter writer(pdfPath);writer.setResolution(96);writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter painter(&writer);painter.fillRect(QRect(0,0,500,700),Qt::red);
            QVERIFY(writer.newPage());painter.fillRect(QRect(0,0,500,700),Qt::blue);
        }
        StudentWindow student({root.path(),20,false,true});student.resize(1420,888);student.show();
        QVERIFY(QTest::qWaitForWindowExposed(&student));
        auto* proof=dynamic_cast<PrintProof*>(student.findChild<QWidget*>("printProof"));QVERIFY(proof);
        auto* reduced=student.findChild<QPushButton*>("studentReduceMotion");reduced->click();
        const auto evidence=qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        QTRY_VERIFY(student.findChild<QLabel*>("quoteEta")->toolTip().contains(QStringLiteral("可立即开始")));
        QVERIFY(student.findChild<QLabel*>("quoteEta")->text().contains(QStringLiteral("可取件")));
        if(!evidence.isEmpty()){QVERIFY(QDir().mkpath(evidence));QTest::qWait(200);QVERIFY(student.grab().save(QDir(evidence).filePath("editorial-reference-native.png")));}
        proof->openReader();
        auto *sampleReader = student.findChild<QDialog*>("documentReader");
        QVERIFY(sampleReader);
        QTest::qWait(80);
        if(!evidence.isEmpty())QVERIFY(sampleReader->grab().save(QDir(evidence).filePath("editorial-reader-fit.png")));
        sampleReader->findChild<QPushButton*>("dialogMaximize")->click();
        QTRY_VERIFY(sampleReader->isMaximized());
        sampleReader->findChild<QPushButton*>("dialogMaximize")->click();
        QTRY_VERIFY(!sampleReader->isMaximized());
        sampleReader->findChild<QPushButton*>("dialogClose")->click();
        QTRY_VERIFY(!student.findChild<QDialog*>("documentReader"));
        student.setDocument(pdfPath);QCOMPARE(proof->documentPages(),2);QVERIFY(proof->previewError().isEmpty());
        const QImage front=proof->renderedPage(0),back=proof->renderedPage(1);QVERIFY(!front.isNull()&&!back.isNull());
        QVERIFY(front.pixelColor(100,100).red()>240);QVERIFY(back.pixelColor(100,100).blue()>240);QVERIFY(front!=back);
        student.findChild<QCheckBox*>("orderDuplex")->setChecked(false);
        student.findChild<QPushButton*>("proofNext")->click();QCOMPARE(proof->currentPage(),1);
        QVERIFY(!student.findChild<QPushButton*>("proofNext")->isEnabled());
        student.findChild<QPushButton*>("proofPrevious")->click();QCOMPARE(proof->currentPage(),0);
        student.findChild<QCheckBox*>("orderDuplex")->setChecked(true);
        student.findChild<QPushButton*>("flipProof")->click();QCOMPARE(proof->currentPage(),1);
        auto* align=student.findChild<QPushButton*>("alignProof");QVERIFY(align);const auto loose=proof->grab().toImage();align->click();QVERIFY(proof->aligned());QVERIFY(loose!=proof->grab().toImage());
        student.findChild<QPushButton*>("magnifyProof")->click();auto* reader=student.findChild<QDialog*>("documentReader");QVERIFY(reader&&reader->isVisible());
        QVERIFY(reader->windowFlags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(reader->windowModality(), Qt::WindowModal);
        QVERIFY(!reader->windowIcon().isNull());
        auto* readerImage=reader->findChild<QLabel*>("readerImage");QVERIFY(readerImage);QVERIFY(readerImage->pixmap().toImage().pixelColor(100,100).blue()>240);
        reader->findChild<QPushButton*>("readerPrevious")->click();QVERIFY(readerImage->pixmap().toImage().pixelColor(100,100).red()>240);
        reader->findChild<QPushButton*>("readerZoom")->click();QCOMPARE(readerImage->pixmap().width(),1000);
        if(!evidence.isEmpty())QVERIFY(reader->grab().save(QDir(evidence).filePath("editorial-pdf-reader.png")));
        reader->findChild<QPushButton*>("readerFit")->click();
        QTRY_VERIFY(readerImage->pixmap().width() < 1000);
        reader->findChild<QPushButton*>("dialogClose")->click();
        QTRY_VERIFY(!student.findChild<QDialog*>("documentReader"));
        const QString imagePath=QDir(root.path()).filePath(QStringLiteral("图片.png"));QImage original(220,150,QImage::Format_RGB32);original.fill(Qt::green);QVERIFY(original.save(imagePath));
        student.setDocument(imagePath);QCOMPARE(proof->documentPages(),1);QVERIFY(proof->renderedPage(0).pixelColor(10,10).green()>240);
        // The reverse of a one-page duplex file is blank, including in the reader.
        proof->setBack(true);
        student.findChild<QPushButton*>("magnifyProof")->click();
        auto* blankReader=student.findChild<QDialog*>("documentReader");QVERIFY(blankReader);
        QCOMPARE(blankReader->findChild<QLabel*>("readerImage")->text(),QStringLiteral("背面留白"));
        QVERIFY(!blankReader->findChild<QPushButton*>("readerNext")->isEnabled());
        blankReader->findChild<QPushButton*>("readerPrevious")->click();
        QVERIFY(!blankReader->findChild<QLabel*>("readerImage")->pixmap().isNull());
        const QString notesPath=QDir(root.path()).filePath("notes.txt");{QFile f(notesPath);QVERIFY(f.open(QIODevice::WriteOnly));for(int i=0;i<120;++i)f.write(QByteArray::number(i)+" Actual document content.\n");}
        student.setDocument(notesPath);QVERIFY(proof->documentPages()>1);QVERIFY(proof->renderedPage(0)!=proof->renderedPage(1));
        QVERIFY(!blankReader->isVisible()); // changing documents closes the stale reader
        QVERIFY(student.findChild<QLabel*>("fileInfo")->text().contains(QString::number(proof->documentPages())));
        const auto oldPages=proof->documentPages();student.setDocument(QDir(root.path()).filePath("absent.pdf"));QCOMPARE(proof->documentPages(),oldPages);
        if(!evidence.isEmpty()){student.resize(920,640);QTest::qWait(100);QVERIFY(student.grab().save(QDir(evidence).filePath("editorial-compact-native.png")));}
        student.findChild<QPushButton*>("removeFile")->click();QCOMPARE(proof->documentPages(),0);
        student.close();QTRY_VERIFY(!student.isVisible());
    }
    void printProofRespondsToSettingsAndKeyboard() {
        BackendDirectory root;
        StudentWindow student({root.path(), 20, false, true}); student.resize(1180, 820); student.show();
        QVERIFY(QTest::qWaitForWindowExposed(&student));
        auto* proof = dynamic_cast<PrintProof*>(student.findChild<QWidget*>("printProof"));
        auto* flip = student.findChild<QPushButton*>("flipProof");
        auto* reduced = student.findChild<QPushButton*>("studentReduceMotion");
        QVERIFY(proof && flip && reduced);
        student.findChild<QCheckBox*>("orderDuplex")->setChecked(false);
        student.findChild<QCheckBox*>("orderColor")->setChecked(false);
        QVERIFY(!flip->isEnabled());
        reduced->click(); QVERIFY(!proof->motionEnabled());
        const auto mono = proof->grab().toImage();
        student.findChild<QCheckBox*>("orderColor")->setChecked(true);
        QVERIFY(proof->specification().contains(QStringLiteral("彩色")));
        QVERIFY(mono != proof->grab().toImage());
        student.findChild<QComboBox*>("orderPaper")->setCurrentIndex(1);
        QVERIFY(proof->specification().startsWith("A3"));
        student.findChild<QCheckBox*>("orderDuplex")->setChecked(true);
        QVERIFY(flip->isEnabled());
        flip->setFocus(); QTest::keyClick(flip, Qt::Key_Space);
        QVERIFY(proof->showingBack());
        QVERIFY(flip->text().contains(QStringLiteral("正面")));
        student.findChild<QSpinBox*>("orderPages")->setValue(1);
        QVERIFY(proof->specification().contains(QStringLiteral("1 页")));
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            QVERIFY(student.grab().save(QDir(evidence).filePath("studio-proof-back.png")));
            // Capture actual native frames for visual inspection of the transition.
            reduced->click(); QVERIFY(proof->motionEnabled());
            proof->setBack(false); QTest::qWait(400);
            flip->click();
            for (int frame = 0; frame < 12; ++frame) {
                QVERIFY(proof->grab().save(QDir(evidence).filePath(QStringLiteral("proof-turn-%1.png").arg(frame, 2, 10, QLatin1Char('0')))));
                QTest::qWait(40);
            }
            QVERIFY(proof->showingBack());
            reduced->click(); QVERIFY(!proof->motionEnabled());
        }
        student.findChild<QSpinBox*>("orderPages")->setValue(3);
        QCOMPARE(proof->sheetCount(), 2);
        auto* unfold = student.findChild<QPushButton*>("unfoldProof");
        auto* next = student.findChild<QPushButton*>("proofNext");
        auto* previous = student.findChild<QPushButton*>("proofPrevious");
        QVERIFY(unfold && next && previous);
        if(proof->expanded())unfold->click();
        const auto stacked = proof->grab().toImage();
        QTest::mouseClick(unfold, Qt::LeftButton); QVERIFY(proof->expanded());
        QVERIFY(stacked != proof->grab().toImage());
        QVERIFY(!previous->isEnabled() && next->isEnabled());
        QTest::mouseClick(next, Qt::LeftButton); QCOMPARE(proof->currentSheet(), 2);
        QVERIFY(!next->isEnabled() && previous->isEnabled());
        QVERIFY(proof->specification().contains(QStringLiteral("背面留白")));
        QTest::mouseClick(previous, Qt::LeftButton); QCOMPARE(proof->currentSheet(), 1);
        if (!evidence.isEmpty()) {
            QVERIFY(student.grab().save(QDir(evidence).filePath("atelier-expanded.png")));
            student.findChild<QSpinBox*>("orderPages")->setValue(8);
            proof->setBack(false);
            reduced->click(); unfold->click(); QTest::qWait(500); unfold->click();
            for (int frame = 0; frame < 16; ++frame) {
                QVERIFY(proof->grab().save(QDir(evidence).filePath(QStringLiteral("proof-spread-%1.png").arg(frame, 2, 10, QLatin1Char('0')))));
                QTest::qWait(40);
            }
            unfold->click();
            for (int frame = 0; frame < 16; ++frame) {
                QVERIFY(proof->grab().save(QDir(evidence).filePath(QStringLiteral("proof-close-%1.png").arg(frame, 2, 10, QLatin1Char('0')))));
                QTest::qWait(40);
            }
            reduced->click();
        }
        student.findChild<QCheckBox*>("orderDuplex")->setChecked(false);
        QVERIFY(!proof->showingBack() && !flip->isEnabled());
        student.resize(920, 640); QTest::qWait(50);
        auto* scroll = student.findChild<QScrollArea*>("orderScroll");
        auto* pay = student.findChild<QPushButton*>("payOrder");
        QTRY_VERIFY(student.centralWidget()->rect().contains(QRect(pay->mapTo(student.centralWidget(), QPoint()), pay->size())));
        QVERIFY(scroll->horizontalScrollBar()->maximum() == 0);
        if (!evidence.isEmpty()) QVERIFY(student.grab().save(QDir(evidence).filePath("studio-student-compact.png")));
        student.raise();student.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&student));
        QTest::keyClick(&student, Qt::Key_2, Qt::ControlModifier);
        QVERIFY(student.findChild<QPushButton*>("navMine")->isChecked());
        QCOMPARE(student.findChild<QTableWidget*>("myOrders")->rowCount(), 0);
        student.close(); QTRY_VERIFY(!student.isVisible());
    }
    void shopWorkflow() {
        BackendDirectory root;
        PrintWindow window({root.path(), 10, true, true}); window.resize(1200, 800); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* fleet = window.findChild<QTableWidget*>("fleetTable");
        QTRY_COMPARE(fleet->rowCount(), 6);
        auto* revenue = window.findChild<QLabel*>("shopMetric0");
        auto* completed = window.findChild<QLabel*>("ordersCompleted");
        QTRY_COMPARE(revenue->text(), QStringLiteral("¥0.00"));
        QCOMPARE(completed->text(), QStringLiteral("0 单"));
        // Device actions follow the selected row instead of acting on nothing.
        auto* toggle = window.findChild<QPushButton*>("togglePrinter");
        auto* edit = window.findChild<QPushButton*>("editPrinter");
        QVERIFY(!toggle->isEnabled() && !edit->isEnabled());
        fleet->selectRow(0);
        QTRY_VERIFY(toggle->isEnabled() && edit->isEnabled());
        QCOMPARE(toggle->text(), QStringLiteral("暂停接单"));
        toggle->click();
        QTRY_COMPARE(fleet->item(0, 3)->text(), QStringLiteral("暂停接单"));
        QTRY_COMPARE(toggle->text(), QStringLiteral("恢复接单"));
        toggle->click();
        QTRY_COMPARE(fleet->item(0, 3)->text(), QStringLiteral("接单中"));
        openForm(window);
        window.findChild<QSpinBox*>("jobPages")->setValue(3);
        window.findChild<QSpinBox*>("jobCopies")->setValue(2);
        window.findChild<QComboBox*>("orderPaper")->setCurrentIndex(1);
        window.findChild<QCheckBox*>("orderColor")->setChecked(true);
        window.findChild<QCheckBox*>("orderDuplex")->setChecked(true);
        window.findChild<QComboBox*>("orderService")->setCurrentIndex(2);
        QTRY_VERIFY(window.findChild<QPushButton*>("submitJob")->isEnabled());
        window.findChild<QPushButton*>("submitJob")->click();
        auto* table = window.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 1);
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("已完成"));
        QCOMPARE(table->model()->index(0, 8).data().toString(), QStringLiteral("¥15.30"));
        QTRY_COMPARE(revenue->text(), QStringLiteral("¥15.30"));
        QCOMPARE(window.findChild<QLabel*>("paid_counter")->text(), QStringLiteral("¥15.30"));
        QCOMPARE(completed->text(), QStringLiteral("1 单"));
        QCOMPARE(window.findChild<QLabel*>("ordersOnTime")->text(), QStringLiteral("1 单"));
        // Search narrows the records; clearing it brings the order back.
        auto* search = window.findChild<QLineEdit*>("queueSearch");
        search->setText(QStringLiteral("没有这样的订单"));
        QCOMPARE(table->model()->rowCount(), 0);
        search->setText(QStringLiteral("实验报告"));
        QCOMPARE(table->model()->rowCount(), 1);
        search->clear();
        auto* policies = window.findChild<QComboBox*>("schedulingPolicy");
        policies->setCurrentIndex(4); QTRY_COMPARE(policies->currentIndex(), 4);
        window.experimentRequested(root.path() + "/experiments.json", 30);
        auto* experiment = window.findChild<QTableWidget*>("experimentTable");
        QTRY_COMPARE_WITH_TIMEOUT(experiment->rowCount(), 21, 15000);
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            for (const auto& name : QStringList{"navShop", "navDevices", "navHistory"}) {
                window.findChild<QPushButton*>(name)->click(); QTest::qWait(80);
                QVERIFY(window.grab().save(QDir(evidence).filePath("shop-" + name + ".png")));
            }
            openForm(window); QTest::qWait(80);
            QVERIFY(window.findChild<QDialog*>("submissionDialog")->grab().save(QDir(evidence).filePath("shop-order.png")));
            window.findChild<QDialog*>("submissionDialog")->reject();
        }
        // A refund needs a selected completed order and an explicit confirmation.
        auto* refund = window.findChild<QPushButton*>("refundOrder");
        window.findChild<QPushButton*>("navHistory")->click();
        QVERIFY(!refund->isEnabled());
        table->selectRow(0);
        QTRY_VERIFY(refund->isEnabled());
        refund->click();
        auto* confirmRefund = window.findChild<QDialog*>("refundDialog");
        QVERIFY(confirmRefund && confirmRefund->isVisible());
        QCOMPARE(revenue->text(), QStringLiteral("¥15.30")); // nothing happens before the confirmation
        if (!evidence.isEmpty()) QVERIFY(confirmRefund->grab().save(QDir(evidence).filePath("shop-refund.png")));
        confirmRefund->findChild<QPushButton*>("confirmAction")->click();
        QTRY_COMPARE(revenue->text(), QStringLiteral("¥0.00"));
        QCOMPARE(window.findChild<QLabel*>("ledgerRefund")->text(), QStringLiteral("¥15.30"));
        QTRY_VERIFY(!refund->isEnabled()); // already refunded in full
        window.close(); QTRY_VERIFY(!window.isVisible());
    }

    void framing() {
        const QByteArray line = QJsonDocument(QJsonObject{{"schema_version", 1},
            {"type", "failed"}, {"job_id", "job-1"}, {"reason", QStringLiteral("中文错误")}})
            .toJson(QJsonDocument::Compact) + '\n';
        // Every byte boundary includes splitting inside a UTF-8 character.
        for (qsizetype split = 0; split <= line.size(); ++split) {
            EventStream stream("job-1");
            auto first = stream.append(line.left(split));
            auto second = stream.append(line.mid(split));
            QCOMPARE(first.events.size() + second.events.size(), 1);
            QVERIFY(first.errors.isEmpty());
            QVERIFY(second.errors.isEmpty());
            QVERIFY(stream.finish().errors.isEmpty());
        }
        EventStream stream("job-1");
        QCOMPARE(stream.append(line + line + line.left(8)).events.size(), 2);
        QCOMPARE(stream.append(line.mid(8)).events.size(), 1);
        QCOMPARE(stream.append(line.trimmed() + "\r\n\n").events.size(), 1);
    }

    void invalidEventsAndRecovery() {
        EventStream stream("job-1");
        const QByteArray good = "{\"schema_version\":1,\"type\":\"progress\",\"job_id\":\"job-1\","
            "\"printed_sheets\":1,\"total_sheets\":8}\n";
        for (const auto& bad : {QByteArray("garbage\n"), QByteArray("[]\n"),
             QByteArray(good).replace("job-1", "job-2"),
             QByteArray(good).replace("\"schema_version\":1", "\"schema_version\":2"),
             QByteArray(good).replace("\"printed_sheets\":1", "\"printed_sheets\":1.5"),
             QByteArray(good).replace("\"printed_sheets\":1", "\"printed_sheets\":9")}) {
            auto result = stream.append(bad + good);
            QCOMPARE(result.events.size(), 1);
            QCOMPARE(result.errors.size(), 1);
        }
        QVERIFY(!stream.append(QByteArray(EventStream::MaxFrameBytes + 1, 'x')).errors.isEmpty());
        QCOMPARE(stream.append("more discarded bytes\n" + good).events.size(), 1);
        stream.append(good.left(10));
        QCOMPARE(stream.finish().errors.size(), 1);
        QVERIFY(stream.finish().errors.isEmpty());
    }

    void interactiveQueueAndCompletion() {
        BackendDirectory root(QDir::tempPath() + QStringLiteral("/界面 测试-XXXXXX"));
        QVERIFY(root.isValid());
        PrintWindow window({root.path(), 10, false});
        window.show();
        auto* title = window.findChild<QLineEdit*>("jobTitle");
        auto* pages = window.findChild<QSpinBox*>("jobPages");
        auto* submit = window.findChild<QPushButton*>("submitJob");
        auto* pause = window.findChild<QPushButton*>("pauseDispatch");
        auto* table = window.findChild<QTableView*>("jobsTable");
        auto* feedback = window.findChild<QLabel*>("feedback");
        QVERIFY(title && pages && submit && pause && table && feedback);
        QTest::mouseClick(pause, Qt::LeftButton);
        openForm(window);
        title->setText("   ");
        QTest::mouseClick(submit, Qt::LeftButton);
        QTRY_VERIFY(feedback->text().contains(QStringLiteral("名称")));
        QCOMPARE(table->model()->rowCount(), 0);
        title->setText(QStringLiteral("中文 交互验收"));
        pages->setValue(2);
        for (int i = 0; i < 3; ++i) { openForm(window); QTest::mouseClick(submit, Qt::LeftButton); }
        QTRY_COMPARE(table->model()->rowCount(), 3);
        window.findChild<QPushButton*>("navHistory")->click();
        table->selectRow(0);
        auto* cancel = window.findChild<QPushButton*>("cancelJob");
        QTRY_VERIFY(cancel->isEnabled());
        QTest::mouseClick(cancel, Qt::LeftButton);
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("已取消"));
        window.findChild<QPushButton*>("navWorkspace")->click();
        QTest::mouseClick(pause, Qt::LeftButton);
        window.findChild<QPushButton*>("navHistory")->click();
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(1, 3).data().toString(), QStringLiteral("已完成"), 10000);
        QTRY_COMPARE(table->model()->index(2, 3).data().toString(), QStringLiteral("已完成"));
        QCOMPARE(QDir(root.path() + "/output").entryList({"*.txt"}, QDir::Files).size(), 2);
        window.close();
        QTRY_VERIFY(!window.isVisible());
    }

    void commandPaletteControlsDispatch() {
        BackendDirectory root;
        PrintWindow window({root.path(), 10, false}); window.show();
        auto* command = window.findChild<QPushButton*>("commandPalette");
        auto* pause = window.findChild<QPushButton*>("pauseDispatch");
        QVERIFY(command && pause);
        bool foundDialog = false;
        QTimer::singleShot(50, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("commandDialog");
            if (!dialog) return;
            auto* query = dialog->findChild<QLineEdit*>("commandQuery");
            auto* choices = dialog->findChild<QListWidget*>("commandChoices");
            if (!query || !choices) { dialog->reject(); return; }
            foundDialog = true; query->setText(QStringLiteral("暂停"));
            QTest::keyClick(query, Qt::Key_Return);
        });
        // A guard prevents a broken modal interaction from hanging the test suite.
        QTimer::singleShot(2000, &window, [&] { auto* dialog=window.findChild<QDialog*>("commandDialog"); if(dialog)dialog->reject(); });
        command->click();
        QVERIFY(foundDialog); QVERIFY(pause->isChecked());
        auto* motion = window.findChild<QPushButton*>("reduceMotion");
        QVERIFY(motion); motion->click();
        QVERIFY(motion->isChecked());
        // These visual controls must not resume dispatch or create jobs.
        QVERIFY(pause->isChecked()); QCOMPARE(window.findChild<QTableView*>("jobsTable")->model()->rowCount(),0);
        window.close(); QTRY_VERIFY(!window.isVisible());
    }

    void largeQueueRemainsInteractive() {
        BackendDirectory root;
        PrintWindow window({root.path(), 10, false});
        window.show();
        QTest::mouseClick(window.findChild<QPushButton*>("pauseDispatch"), Qt::LeftButton);
        QJsonArray jobs;
        for (int i = 0; i < 5000; ++i) {
            jobs.append(QJsonObject{{"title", QStringLiteral("批量 %1").arg(i)}, {"pages", 1}, {"copies", 1}});
        }
        QFile workload(root.path() + "/workload.json");
        QVERIFY(workload.open(QIODevice::WriteOnly));
        workload.write(QJsonDocument(jobs).toJson(QJsonDocument::Compact));
        workload.close();
        int ticks = 0;
        qint64 longestGap = 0;
        QElapsedTimer elapsed;
        elapsed.start();
        QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&] {
            ++ticks;
            longestGap = std::max(longestGap, elapsed.restart());
        });
        timer.start(5);
        window.importFile(workload.fileName());
        auto* table = window.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->rowCount(), 5000, 15000);
        QVERIFY(ticks > 0);
        auto* title = window.findChild<QLineEdit*>("jobTitle");
        openForm(window);
        title->clear();
        QTest::keyClicks(title, "still responsive");
        QCOMPARE(title->text(), QString("still responsive"));
        QVERIFY2(longestGap < 2000, "GUI event loop stalled for two seconds during import");
        qInfo("5000-job import: heartbeat ticks=%d, longest observed gap=%lld ms", ticks, static_cast<long long>(longestGap));
        // No executor was started while the local queue was paused.
        QVERIFY(QDir(root.path() + "/output").entryList({"*.txt"}, QDir::Files).isEmpty());
        window.findChild<QDialog*>("submissionDialog")->reject();
    }

    void paperWorkbenchTracksPolicyAndRealJobs() {
        BackendDirectory root;
        PrintWindow window({root.path(), 100, false});
        window.resize(1380, 860); window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* pause = window.findChild<QPushButton*>("pauseDispatch");
        pause->click();
        window.submitRequested(QStringLiteral("操作系统实验报告"), 30, 1, 1);
        window.submitRequested(QStringLiteral("实验数据"), 2, 1, 0);
        window.submitRequested(QStringLiteral("阅读材料"), 10, 1, 9);
        auto* table = window.findChild<QTableView*>("jobsTable");
        auto* canvas = dynamic_cast<PaperWorkspace*>(window.findChild<QWidget*>("paperWorkspace"));
        auto* policy = window.findChild<QComboBox*>("schedulingPolicy");
        QVERIFY(canvas && policy);
        QTRY_COMPARE(table->model()->rowCount(), 3);
        const auto id = [&](int row) { return table->model()->index(row, 0).data(Qt::UserRole).toString(); };
        QTRY_COMPARE(canvas->waitingIds().value(0), id(0));
        policy->setCurrentIndex(1);
        QTRY_COMPARE(canvas->waitingIds().value(0), id(1));
        policy->setCurrentIndex(2);
        QTRY_COMPARE(canvas->waitingIds().value(0), id(2));
        window.findChild<QPushButton*>("waitingJob0")->click();
        QCOMPARE(canvas->selectedId(), id(2));
        policy->setCurrentIndex(0);
        QTRY_COMPARE(canvas->waitingIds().value(0), id(0));
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        auto screenshot = [&](const QString& name) {
            return evidence.isEmpty() || (QDir().mkpath(evidence) && window.grab().save(QDir(evidence).filePath(name)));
        };
        QTest::qWait(450); QVERIFY(screenshot("paper-01-queued.png"));
        pause->click();
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("打印中"));
        policy->setCurrentIndex(2);
        // Policy changes must not preempt the already running long task.
        QCOMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("打印中"));
        window.findChild<QPushButton*>("waitingJob0")->click();
        QTest::qWait(180);
        QVERIFY(screenshot("paper-02-running.png"));
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(0, 3).data().toString(), QStringLiteral("已完成"), 10000);
        QTRY_COMPARE(table->model()->index(2, 3).data().toString(), QStringLiteral("已完成"));
        QTRY_VERIFY(canvas->waitingIds().isEmpty());
        QCOMPARE(QDir(root.path() + "/output").entryList({"*.txt"}, QDir::Files).size(), 3);
        QVERIFY(screenshot("paper-03-completed.png"));
        openForm(window);
        if (!evidence.isEmpty()) QVERIFY(window.findChild<QDialog*>("submissionDialog")->grab().save(QDir(evidence).filePath("paper-04-form.png")));
        window.findChild<QDialog*>("submissionDialog")->reject();
        window.resize(1000, 680);
        auto* scroll = window.findChild<QScrollArea*>("workbenchScroll");
        scroll->ensureWidgetVisible(pause);
        QTRY_VERIFY(scroll->viewport()->rect().contains(QRect(pause->mapTo(scroll->viewport(), QPoint(0, 0)), pause->size())));
        QVERIFY(screenshot("paper-05-compact.png"));
        window.close(); QTRY_VERIFY(!window.isVisible());
    }

    void invalidBatchIsAtomicAndReportIsWritten() {
        BackendDirectory root;
        PrintWindow window({root.path(), 10, false});
        window.show();
        QTest::mouseClick(window.findChild<QPushButton*>("pauseDispatch"), Qt::LeftButton);
        QFile workload(root.path() + "/invalid.json");
        QVERIFY(workload.open(QIODevice::WriteOnly));
        workload.write("[{\"title\":\"valid\",\"pages\":1},{\"title\":\"bad\",\"pages\":1.5}]");
        workload.close();
        window.importFile(workload.fileName());
        QTRY_VERIFY(window.findChild<QLabel*>("feedback")->text().contains(QStringLiteral("未导入任何任务")));
        QCOMPARE(window.findChild<QTableView*>("jobsTable")->model()->rowCount(), 0);
        window.exportRequested(root.path() + "/report.json");
        QTRY_VERIFY(QFile::exists(root.path() + "/report.json"));
        QFile report(root.path() + "/report.json");
        QVERIFY(report.open(QIODevice::ReadOnly));
        const auto json = QJsonDocument::fromJson(report.readAll()).object();
        QCOMPARE(json.value("mode").toString(), QString("persistent_simulation"));
        QCOMPARE(json.value("completed").toInt(), 0);
    }

    void closeCannotRaceSubmission() {
        BackendDirectory root;
        QString id;
        {
            PrintWindow window({root.path(), 30, false}); window.show();
            window.submitRequested(QStringLiteral("关闭前提交"), 100, 1, 0);
            window.close(); // Serialized after submit, before its first visual snapshot.
            QTRY_VERIFY(!window.isVisible());
            CoordinatorClient client(root.path());
            const auto jobs = client.request("snapshot").value("jobs").toArray();
            QCOMPARE(jobs.size(), 1); id = jobs[0].toObject().value("job_id").toString();
        }
        PrintWindow reopened({root.path(), 30, false}); reopened.show();
        auto* table = reopened.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 1);
        QCOMPARE(table->model()->index(0, 0).data(Qt::UserRole).toString(), id);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(0, 3).data().toString(), QStringLiteral("已完成"), 10000);
        reopened.close(); QTRY_VERIFY(!reopened.isVisible());
    }

    static QJsonObject readObject(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    void persistentPoliciesAndReport() {
        BackendDirectory root(QDir::tempPath() + QStringLiteral("/持久 验收-XXXXXX"));
        {
            PrintWindow window({root.path(), 40, false});
            QSignalSpy ready(&window, &PrintWindow::backendReady);
            window.show();
            QTRY_VERIFY_WITH_TIMEOUT(!ready.isEmpty(), 15000);
            window.pauseRequested(true);
            CoordinatorClient client(root.path());
            QJsonObject paused;
            // Do not submit short jobs until the durable pause is visible.
            // The UI disables mutation before readiness; the test must too.
            QTRY_VERIFY((paused = client.request("snapshot")).value("paused").toBool());
            window.submitRequested(QStringLiteral("先来的大任务"), 40, 1, 0);
            window.submitRequested(QStringLiteral("短任务"), 2, 1, 0);
            window.submitRequested(QStringLiteral("紧急任务"), 10, 1, 100);
            auto* table = window.findChild<QTableView*>("jobsTable");
            QTRY_COMPARE(table->model()->rowCount(), 3);
            const auto first = client.request("snapshot");
            QVERIFY(first.value("paused").toBool());
            const auto rows = first.value("jobs").toArray();
            const auto id = [&](int i) { return rows[i].toObject().value("job_id").toString(); };
            for (int policy = 0; policy < 3; ++policy) {
                window.policyRequested(policy);
                auto* picker = window.findChild<QComboBox*>("schedulingPolicy");
                QTRY_COMPARE(picker->currentIndex(), policy);
                QJsonObject snap;
                const QStringList names{"fcfs", "sjf", "priority"};
                QTRY_VERIFY((snap = client.request("snapshot")).value("policy").toString() == names[policy]);
                QCOMPARE(snap.value("waiting_order").toArray()[0].toString(), id(policy));
                for (const auto& j : snap.value("jobs").toArray()) QCOMPARE(j.toObject().value("attempts").toInt(), 0);
            }
            // Backend validation is atomic too, independent of the GUI's input checks.
            const QJsonObject valid{{"job_id", "atomic-new"}, {"title", "valid"}, {"pages", 1}, {"copies", 1}};
            auto duplicate = valid; duplicate.insert("job_id", id(0));
            QCOMPARE(client.request("submit_batch", {{"jobs", QJsonArray{valid, duplicate}}}).value("type").toString(), QString("error"));
            QCOMPARE(client.request("snapshot").value("jobs").toArray().size(), 3);
            window.close(); QTRY_VERIFY(!window.isVisible());
        }
        PrintWindow reopened({root.path(), 40, false}); reopened.show();
        auto* table = reopened.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 3);
        QTRY_VERIFY(reopened.findChild<QPushButton*>("pauseDispatch")->isChecked());
        QTRY_COMPARE(reopened.findChild<QComboBox*>("schedulingPolicy")->currentIndex(), 2);
        reopened.pauseRequested(false);
        for (int i = 0; i < 3; ++i)
            QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(i, 3).data().toString(), QStringLiteral("已完成"), 15000);
        const auto path = root.path() + "/audit.json";
        reopened.exportRequested(path);
        QTRY_VERIFY(QFile::exists(path));
        const auto report = readObject(path);
        QCOMPARE(report.value("mode").toString(), QString("persistent_simulation"));
        QCOMPARE(report.value("completed").toInt(), 3);
        QCOMPARE(report.value("cancelled").toInt(), 0);
        const double start = report.value("window_start_ms").toDouble();
        const double end = report.value("window_end_ms").toDouble();
        double waits = 0, turns = 0, busy = 0, maxWait = 0;
        for (const auto& item : report.value("jobs").toArray()) {
            const auto j = item.toObject();
            const double submitted = j.value("submitted_ms").toDouble();
            const double started = j.value("first_started_ms").toDouble();
            const double finished = j.value("finished_ms").toDouble();
            const double wait = started - submitted;
            QVERIFY(started >= submitted && finished >= started && finished <= end);
            waits += wait; turns += finished - submitted; maxWait = std::max(maxWait, wait);
            QCOMPARE(j.value("accumulated_wait_ms").toDouble(), wait);
            QCOMPARE(j.value("attempts").toInt(), 1);
            const auto runs = j.value("runs").toArray(); QCOMPARE(runs.size(), 1);
            busy += runs[0].toObject().value("stopped_ms").toDouble() - runs[0].toObject().value("started_ms").toDouble();
            QFile output(root.path() + "/output/" + j.value("job_id").toString() + ".txt");
            QVERIFY(output.open(QIODevice::ReadOnly));
            QCOMPARE(output.readAll().count("sheet="), j.value("pages").toInt() * j.value("copies").toInt());
            QFile events(root.path() + "/output/" + j.value("job_id").toString() + ".events.jsonl");
            QVERIFY(events.open(QIODevice::ReadOnly));
            QCOMPARE(events.readAll().count("worker_started"), 1);
        }
        const auto near = [&](const char* field, double expected) {
            return std::abs(report.value(field).toDouble() - expected) < 1e-6 * std::max(1.0, std::abs(expected));
        };
        QVERIFY(near("average_first_wait_ms", waits / 3));
        QVERIFY(near("average_total_queue_wait_ms", waits / 3));
        QVERIFY(near("average_turnaround_ms", turns / 3));
        QVERIFY(near("max_first_wait_ms", maxWait));
        QVERIFY(near("busy_printer_ms", busy));
        QVERIFY(near("throughput_per_second", 3000.0 / (end - start)));
        QVERIFY(near("utilization", busy / (2 * (end - start))));
        reopened.close(); QTRY_VERIFY(!reopened.isVisible());
        PrintWindow again({root.path(), 40, false}); again.show();
        again.exportRequested(root.path() + "/after-reopen.json");
        QTRY_VERIFY(QFile::exists(root.path() + "/after-reopen.json"));
        const auto restored = readObject(root.path() + "/after-reopen.json");
        QCOMPARE(restored.value("completed").toInt(), 3);
        QCOMPARE(restored.value("window_start_ms"), report.value("window_start_ms"));
        QCOMPARE(restored.value("average_first_wait_ms"), report.value("average_first_wait_ms"));
        again.close(); QTRY_VERIFY(!again.isVisible());
    }

    void backendCrashDuringCancellation() {
        BackendDirectory root;
        QProcess backend;
        backend.setProgram(cps::siblingExecutable("print-coordinator"));
        backend.setArguments({"--state-dir", root.path(), "--page-ms", "20"});
        backend.start(); QVERIFY(backend.waitForStarted(5000));
        CoordinatorClient client(root.path());
        QJsonObject initial;
        QTRY_VERIFY(!(initial = client.request("snapshot")).isEmpty());
        const double originalPid = initial.value("coordinator_pid").toDouble();
        PrintWindow window({root.path(), 20, false}); window.show();
        window.submitRequested("cancel across crash", 200, 1, 0);
        window.submitRequested("recover remaining work", 100, 1, 0);
        auto* table = window.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 2);
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("打印中"));
        const auto cancelledId = table->model()->index(0, 0).data(Qt::UserRole).toString();
        // Crash immediately after the durable cancellation ACK, before its terminal event.
        QCOMPARE(client.request("cancel", {{"job_id", cancelledId}}).value("type").toString(), QString("accepted"));
        backend.kill(); QVERIFY(backend.waitForFinished(5000)); client.disconnect();
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(0, 3).data().toString(), QStringLiteral("已取消"), 20000);
        QTRY_COMPARE_WITH_TIMEOUT(table->model()->index(1, 3).data().toString(), QStringLiteral("已完成"), 20000);
        const auto recovered = client.request("snapshot");
        QVERIFY(recovered.value("coordinator_pid").toDouble() != originalPid);
        QCOMPARE(recovered.value("metrics").toObject().value("cancelled").toInt(), 1);
        QCOMPARE(recovered.value("metrics").toObject().value("completed").toInt(), 1);
        for (const auto& item : recovered.value("jobs").toArray()) {
            const auto job = item.toObject();
            if (job.value("job_id").toString() == cancelledId) {
                QCOMPARE(job.value("attempts").toInt(), 1);
                QVERIFY(!QFile::exists(root.path() + "/output/" + cancelledId + ".txt"));
            } else {
                QFile file(root.path() + "/output/" + job.value("job_id").toString() + ".txt");
                QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll().count("sheet="), 100);
            }
        }
        window.close(); QTRY_VERIFY(!window.isVisible());
    }

    void runningCancelFaultAndReconnect() {
        BackendDirectory root;
        PrintWindow window({root.path(), 30, true});
        window.show();
        // Cocoa activation/exposure is asynchronous. Don't start short-lived
        // work before the native window is available for interaction.
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* pages = window.findChild<QSpinBox*>("jobPages");
        pages->setValue(1000); // Keep both jobs alive until explicitly cancelled.
        auto* submit = window.findChild<QPushButton*>("submitJob");
        openForm(window);
        QTest::mouseClick(submit, Qt::LeftButton);
        openForm(window);
        QTest::mouseClick(submit, Qt::LeftButton);
        window.findChild<QPushButton*>("navHistory")->click();
        auto* table = window.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 2);
        table->selectRow(0);
        auto* fault = window.findChild<QPushButton*>("injectFault");
        auto* disconnect = window.findChild<QPushButton*>("disconnectControl");
        auto* cancel = window.findChild<QPushButton*>("cancelJob");
        auto* log = window.findChild<QPlainTextEdit*>();
        QVERIFY(fault && disconnect && cancel && log);
        QTRY_VERIFY2(fault->isEnabled(), qPrintable(QStringLiteral("state=%1 ready=%2 current=%3; %4")
            .arg(table->model()->index(0, 3).data().toString())
            .arg(table->model()->index(0, 0).data(Qt::UserRole + 2).toBool())
            .arg(table->currentIndex().row()).arg(log->toPlainText())));
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        auto screenshot = [&](const QString& name) {
            if (evidence.isEmpty()) return true;
            return QDir().mkpath(evidence) && window.grab().save(QDir(evidence).filePath(name));
        };
        QVERIFY(screenshot("01-connected.png"));
        QTest::mouseClick(fault, Qt::LeftButton);
        QTRY_VERIFY(log->toPlainText().contains("worker_restarting"));
        QTest::mouseClick(disconnect, Qt::LeftButton);
        QTRY_VERIFY(log->toPlainText().contains(QStringLiteral("控制连接断开")));
        QTRY_VERIFY(cancel->isEnabled());
        QVERIFY(screenshot("02-reconnected.png"));
        QTest::mouseClick(cancel, Qt::LeftButton);
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("已取消"));
        table->selectRow(1);
        QTRY_VERIFY(cancel->isEnabled());
        QTest::mouseClick(cancel, Qt::LeftButton);
        QTRY_COMPARE(table->model()->index(1, 3).data().toString(), QStringLiteral("已取消"));
        QVERIFY(screenshot("03-cancelled.png"));
        QVERIFY(QDir(root.path() + "/output").entryList({"*.txt"}, QDir::Files).isEmpty());
        window.close();
        QTRY_VERIFY(!window.isVisible());
    }

    void ownerConsoleShowsForecast() {
        BackendDirectory root;
        PrintWindow window({root.path(), 40, false});
        QSignalSpy backendReady(&window, &PrintWindow::backendReady);
        window.show();
        auto* pause = window.findChild<QPushButton*>("pauseDispatch");
        QVERIFY(pause);
        // Receive a connected snapshot first; the widget's initial enabled
        // property by itself does not prove the coordinator is ready.
        QTRY_VERIFY_WITH_TIMEOUT(!backendReady.isEmpty(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(pause->isEnabled(), 10000);
        window.pauseRequested(true);
        QTRY_VERIFY_WITH_TIMEOUT(pause->isChecked(), 10000);
        for (int i = 0; i < 3; ++i) window.submitRequested(QStringLiteral("预计 %1").arg(i), 100, 1, 0);
        auto* table = window.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 3);
        QTRY_VERIFY(table->model()->index(2, 5).data().toString().contains(QStringLiteral("派发已暂停")));
        CoordinatorClient client(root.path());
        const auto rows = client.request("snapshot").value("jobs").toArray();
        QCOMPARE(rows.size(), 3);
        // Two printers at 40 ms per page: the third order starts when the first one ends.
        const auto first = rows[0].toObject(), third = rows[2].toObject();
        QCOMPARE(third.value("jobs_ahead").toInt(), 2);
        QCOMPARE(third.value("estimated_start_ms").toDouble() - first.value("estimated_start_ms").toDouble(), 4000.0);
        QCOMPARE(third.value("estimated_finish_ms").toDouble() - third.value("estimated_start_ms").toDouble(), 4000.0);
        window.pauseRequested(false);
        QTRY_COMPARE(table->model()->index(0, 3).data().toString(), QStringLiteral("打印中"));
        QTRY_VERIFY(table->model()->index(0, 5).data().toString().startsWith(QStringLiteral("预计")));
        QTRY_VERIFY(table->model()->index(2, 5).data().toString().contains(QStringLiteral("前面 0 单")));
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            window.resize(1280, 820); QTest::qWait(500);
            QVERIFY(window.grab().save(QDir(evidence).filePath("owner-forecast-workspace.png")));
            window.findChild<QPushButton*>("navHistory")->click(); QTest::qWait(80);
            QVERIFY(window.grab().save(QDir(evidence).filePath("owner-forecast-records.png")));
        }
        window.pauseRequested(true); // keep teardown from dispatching the waiting order
        window.close(); QTRY_VERIFY(!window.isVisible());
    }

    void studentOrderPaymentAndForecast() {
        BackendDirectory root(QDir::tempPath() + QStringLiteral("/学生 自助-XXXXXX"));
        QVERIFY(root.isValid());
        const auto pdfPath = root.path() + QStringLiteral("/实验 报告.pdf");
        const auto textPath = root.path() + QStringLiteral("/笔记.txt");
        {
            QFile pdf(pdfPath); QVERIFY(pdf.open(QIODevice::WriteOnly));
            pdf.write("%PDF-1.4\n1 0 obj<</Type/Pages/Count 2/Kids[2 0 R 3 0 R]>>endobj\n"
                      "2 0 obj<</Type /Page/Parent 1 0 R>>endobj\n3 0 obj<</Type/Page/Parent 1 0 R>>endobj\n");
            QFile notes(textPath); QVERIFY(notes.open(QIODevice::WriteOnly));
            notes.write(QByteArray("line\n").repeated(120));
        }
        QCOMPARE(inspectDocument(pdfPath).pages, 2);
        QCOMPARE(inspectDocument(textPath).pages, 3);
        QVERIFY(!inspectDocument(root.path() + "/missing.pdf").error.isEmpty());

        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        auto screenshot = [&](QWidget* widget, const QString& name) {
            return evidence.isEmpty() || (QDir().mkpath(evidence) && widget->grab().save(QDir(evidence).filePath(name)));
        };
        StudentWindow student({root.path(), 20, false, true}, QStringLiteral("2024000001"));
        student.resize(1180, 820); student.show();
        student.findChild<QCheckBox*>("orderDuplex")->setChecked(false);
        QVERIFY(QTest::qWaitForWindowExposed(&student));
        auto* pay = student.findChild<QPushButton*>("payOrder");
        auto* eta = student.findChild<QLabel*>("quoteEta");
        auto* price = student.findChild<QLabel*>("quotePrice");
        auto* feedback = student.findChild<QLabel*>("feedback");
        auto* orders = student.findChild<QTableWidget*>("myOrders");
        auto* identity = student.findChild<QLineEdit*>("studentId");
        QVERIFY(pay && eta && price && feedback && orders && identity);
        QTRY_VERIFY(eta->toolTip().contains(QStringLiteral("可立即开始")));
        QVERIFY(!pay->isEnabled()); // nothing uploaded yet
        auto* hint = student.findChild<QLabel*>("payHint");
        QVERIFY(hint->isVisible() && hint->text().contains(QStringLiteral("选择要打印的文件")));
        QVERIFY(screenshot(&student, "student-00-empty.png"));

        // The summary strip scales with the editorial canvas. Check the actual
        // polished fonts against label rectangles, including the CJK idle text.
        auto checkSummaryText = [&](const QString& state) -> QString {
            for (const QSize size : {QSize(920, 640), QSize(1180, 820), QSize(1420, 888)}) {
                student.resize(size);
                for (const auto* navigation : {"navMine", "navQueue"}) {
                    student.findChild<QPushButton*>(navigation)->click();
                    QTest::qWait(30);
                    auto* pages = student.findChild<QWidget*>("studentPages");
                    for (auto* text : student.findChildren<QLabel*>()) {
                        const auto role = text->property("role").toString();
                        if (!text->isVisible() || (!role.startsWith("metric") && !pages->isAncestorOf(text))
                            || text->text().isEmpty()) continue;
                        const auto metrics = text->fontMetrics();
                        const auto area = text->contentsRect();
                        const auto required = text->wordWrap()
                            ? metrics.boundingRect(QRect(0, 0, area.width(), 100000), Qt::TextWordWrap, text->text()).size()
                            : QSize(metrics.horizontalAdvance(text->text()), metrics.height());
                        const auto detail = QStringLiteral("%1 %2 %3: '%4', label %5x%6, text %7x%8")
                            .arg(state, navigation, QString::number(size.width()), text->text())
                            .arg(area.width()).arg(area.height())
                            .arg(required.width()).arg(required.height());
                        if (area.height() < required.height() || area.width() < required.width())
                            return detail;
                    }
                    if (!screenshot(&student, QStringLiteral("student-summary-%1-%2-%3.png")
                        .arg(state, navigation, QString::number(size.width())))) return "Cannot save summary screenshot";
                }
            }
            student.resize(1180, 820);
            student.findChild<QPushButton*>("navOrder")->click();
            return {};
        };
        QTRY_COMPARE(student.findChild<QLabel*>("studentMetric1")->text(), QStringLiteral("无需等待"));
        qInfo() << "Student text checks, device pixel ratio:" << student.devicePixelRatioF();
        const auto idleTextFailure = checkSummaryText("idle");
        QVERIFY2(idleTextFailure.isEmpty(), qPrintable(idleTextFailure));

        // Render the reference composition with a real metadata selection and backend quote.
        if (!evidence.isEmpty()) {
            student.setDocument(textPath);
            student.findChild<QSpinBox*>("orderPages")->setValue(8);
            student.findChild<QCheckBox*>("orderColor")->setChecked(true);
            student.findChild<QCheckBox*>("orderDuplex")->setChecked(true);
            student.resize(1440, 900);
            QTRY_COMPARE(price->text(), QStringLiteral("¥11.40"));
            QTRY_VERIFY(pay->isEnabled()); QTest::qWait(300);
            QVERIFY(screenshot(&student, "atelier-reference-live.png"));
            student.findChild<QCheckBox*>("orderDuplex")->setChecked(false);
            student.resize(1180, 820);
        }

        student.setDocument(pdfPath);
        QCOMPARE(student.findChild<QSpinBox*>("orderPages")->value(), 2);
        QCOMPARE(student.findChild<QLineEdit*>("orderTitle")->text(), QStringLiteral("实验 报告.pdf"));
        student.findChild<QSpinBox*>("orderCopies")->setValue(3);
        student.findChild<QCheckBox*>("orderColor")->setChecked(true);
        student.findChild<QPushButton*>("serviceFlexible")->click();
        QCOMPARE(price->text(), QStringLiteral("¥7.65")); // 6 colour sides at 1.50, 85%
        QTRY_VERIFY(pay->isEnabled());
        QVERIFY(!hint->isVisible());
        QTest::qWait(80); // let the layout settle before the evidence screenshot
        QVERIFY(screenshot(&student, "student-01-order.png"));
        student.findChild<QPushButton*>("pay_alipay")->click();
        pay->click();
        auto* dialog = student.findChild<QDialog*>("paymentDialog");
        QVERIFY(dialog && dialog->isVisible());
        QVERIFY(dialog->windowFlags().testFlag(Qt::FramelessWindowHint));
        QVERIFY(!dialog->windowIcon().isNull());
        for (const auto *method : {"wechat", "alipay", "campus_card"}) {
            auto *button = dialog->findChild<QPushButton*>(QStringLiteral("paymentMethod_") + method);
            QVERIFY(button && !button->icon().isNull());
            button->click();
            QCOMPARE(dialog->findChild<QFrame *>("paymentReceipt")->property("method").toString(), QString(method));
        }
        dialog->findChild<QPushButton*>("paymentMethod_wechat")->click();
        QVERIFY(screenshot(dialog, "student-02-wechat.png"));
        dialog->findChild<QPushButton*>("paymentMethod_alipay")->click();
        QVERIFY(student.findChild<QPushButton*>("pay_alipay")->isChecked());
        QVERIFY(screenshot(dialog, "student-02-payment.png"));
        dialog->findChild<QPushButton*>("confirmPayment")->click();
        QVERIFY(dialog->property("busy").toBool());
        QVERIFY(!dialog->findChild<QPushButton*>("paymentMethod_wechat")->isEnabled());
        QVERIFY(!dialog->findChild<QPushButton*>("dialogClose")->isEnabled());
        QVERIFY(!dialog->close()); // a submitted payment must finish before dismissal
        QVERIFY(dialog->isVisible());
        QTRY_COMPARE(orders->rowCount(), 1);
        QTRY_VERIFY(feedback->text().contains(QStringLiteral("支付成功")));
        QTRY_VERIFY(!student.findChild<QDialog*>("paymentDialog"));
        QCOMPARE(orders->currentRow(), 0); // the order just paid for is selected
        QCOMPARE(orders->item(0, 2)->text(), QStringLiteral("¥7.65 · 支付宝已支付"));
        QTRY_COMPARE_WITH_TIMEOUT(orders->item(0, 3)->text(), QStringLiteral("已完成"), 10000);
        QCOMPARE(orders->item(0, 6)->text().size(), 6);
        QTest::qWait(80);
        const auto tableImage = orders->viewport()->grab().toImage().scaled(orders->viewport()->size());
        auto redPixels = [&](QRect area) {
            int count = 0;
            for (int y = area.top(); y <= area.bottom(); ++y)
                for (int x = area.left(); x <= area.right(); ++x) {
                    const auto c = tableImage.pixelColor(x, y);
                    if (c.red() > 120 && c.red() > c.green() * 1.4 && c.red() > c.blue() * 1.4) ++count;
                }
            return count;
        };
        for (const int column : {3, 5, 6})
            QVERIFY(redPixels(orders->visualItemRect(orders->item(0, column))) > 15);
        const auto selected = tableImage.pixelColor(orders->visualItemRect(orders->item(0, 0)).topLeft() + QPoint(3, 3));
        QVERIFY(selected.red() > selected.green() && selected.green() > selected.blue());
        QVERIFY(screenshot(&student, "student-02-completed.png"));
        QVERIFY(!pay->isEnabled()); // the next order needs its own upload

        CoordinatorClient client(root.path());
        auto snapshot = client.request("snapshot");
        const auto paid = snapshot.value("jobs").toArray()[0].toObject();
        QCOMPARE(paid.value("payment").toString(), QString("alipay"));
        QCOMPARE(paid.value("customer").toString(), QString("2024000001"));
        QCOMPARE(paid.value("quoted_cents").toInt(), 765);

        // A paused shop still quotes and accepts orders; the estimate is relative to resuming.
        QCOMPARE(client.request("set_paused", {{"paused", true}}).value("type").toString(), QString("accepted"));
        student.findChild<QPushButton*>("navOrder")->click();
        QTRY_VERIFY(eta->text().contains(QStringLiteral("店铺暂停派发")));
        student.findChild<QPushButton*>("pay_wechat")->click();
        for (int count = 2; count <= 3; ++count) {
            student.setDocument(textPath);
            auto* proof=dynamic_cast<PrintProof*>(student.findChild<QWidget*>("printProof"));
            QVERIFY(proof);QCOMPARE(student.findChild<QSpinBox*>("orderPages")->value(),proof->documentPages());
            // This pricing scenario deliberately requests three pages; actual
            // text pagination now comes from the native document renderer.
            student.findChild<QSpinBox*>("orderPages")->setValue(3);
            QTRY_VERIFY(pay->isEnabled());
            pay->click();
            auto* confirm = student.findChild<QPushButton*>("confirmPayment");
            QVERIFY(confirm);
            confirm->click();
            QTRY_COMPARE(orders->rowCount(), count);
            QTRY_VERIFY(!student.findChild<QDialog*>("paymentDialog"));
        }
        // Newest first: row 0 is the last order, row 1 the one placed before it.
        QTRY_VERIFY(orders->item(1, 4)->text().contains(QStringLiteral("派发已暂停")));
        const auto pausedTextFailure = checkSummaryText("paused");
        QVERIFY2(pausedTextFailure.isEmpty(), qPrintable(pausedTextFailure));
        QCOMPARE(orders->item(0, 2)->text(), QStringLiteral("¥11.48 · 微信已支付"));
        QVERIFY(screenshot(&student, "student-03-orders.png"));
        auto* waiting = student.findChild<QTableWidget*>("shopQueue");
        student.findChild<QPushButton*>("navQueue")->click();
        QTRY_COMPARE(waiting->rowCount(), 2);
        QVERIFY(waiting->item(0, 1)->text().startsWith(QStringLiteral("我的订单")));
        QCOMPARE(student.findChild<QTableWidget*>("shopDevices")->rowCount(), 6);
        QVERIFY(screenshot(&student, "student-04-queue.png"));
        identity->setText(QStringLiteral("2024000002"));
        QCOMPARE(orders->rowCount(), 0); // another student sees no titles and no orders
        QCOMPARE(waiting->item(0, 1)->text(), QStringLiteral("其他订单"));
        identity->setText(QStringLiteral("2024000001"));
        QCOMPARE(orders->rowCount(), 3);

        student.findChild<QPushButton*>("navMine")->click();
        auto* cancel = student.findChild<QPushButton*>("cancelOrder");
        orders->selectRow(1);
        QTRY_VERIFY(cancel->isEnabled());
        cancel->click();
        auto* confirmCancel = student.findChild<QDialog*>("cancelDialog");
        QVERIFY(confirmCancel && confirmCancel->isVisible());
        QCOMPARE(orders->item(1, 3)->text(), QStringLiteral("等待中")); // still queued until confirmed
        QVERIFY(screenshot(confirmCancel, "student-06-cancel.png"));
        confirmCancel->findChild<QPushButton*>("confirmAction")->click();
        QTRY_COMPARE(orders->item(1, 3)->text(), QStringLiteral("已取消"));
        QVERIFY(orders->item(1, 2)->text().contains(QStringLiteral("已原路退款")));
        QTRY_VERIFY(feedback->text().contains(QStringLiteral("已提交取消")));
        QVERIFY(!cancel->isEnabled());
        QCOMPARE(client.request("set_paused", {{"paused", false}}).value("type").toString(), QString("accepted"));
        QTRY_COMPARE_WITH_TIMEOUT(orders->item(0, 3)->text(), QStringLiteral("已完成"), 10000);
        QTRY_COMPARE(student.findChild<QLabel*>("studentMetric3")->text(), QStringLiteral("2 单"));
        const auto readyTextFailure = checkSummaryText("ready");
        QVERIFY2(readyTextFailure.isEmpty(), qPrintable(readyTextFailure));

        // The owner console reads the same coordinator state.
        PrintWindow boss({root.path(), 20, false, true}); boss.resize(1200, 800); boss.show();
        auto* table = boss.findChild<QTableView*>("jobsTable");
        QTRY_COMPARE(table->model()->rowCount(), 3);
        QCOMPARE(table->model()->index(0, 8).data().toString(), QStringLiteral("¥7.65 · 支付宝"));
        QTRY_COMPARE(boss.findChild<QLabel*>("paid_wechat")->text(), QStringLiteral("¥11.48"));
        QCOMPARE(boss.findChild<QLabel*>("paid_alipay")->text(), QStringLiteral("¥7.65"));
        // The counter finds a student's order by the pickup code shown in the student client.
        boss.findChild<QLineEdit*>("queueSearch")->setText(orders->item(2, 6)->text().toLower());
        QCOMPARE(table->model()->rowCount(), 1);
        QCOMPARE(table->model()->index(0, 8).data().toString(), QStringLiteral("¥7.65 · 支付宝"));
        boss.findChild<QLineEdit*>("queueSearch")->clear();
        QCOMPARE(boss.findChild<QLabel*>("shopMetric0")->text(), QStringLiteral("¥19.13"));
        QCOMPARE(boss.findChild<QLabel*>("shopMetric3")->text(), QStringLiteral("0 / 6 台打印中"));
        QVERIFY(boss.findChild<QPushButton*>("openStudent"));
        QVERIFY(screenshot(&boss, "student-05-owner.png"));
        boss.close(); QTRY_VERIFY(!boss.isVisible());
        student.close(); QTRY_VERIFY(!student.isVisible());
    }

    void demoControlsAbsentInNormalMode() {
        BackendDirectory root;
        PrintWindow window({root.path(), 10, false});
        QVERIFY(!window.findChild<QPushButton*>("injectFault"));
        QVERIFY(!window.findChild<QPushButton*>("disconnectControl"));
    }

    void compactWindowKeepsControlsReachable() {
        BackendDirectory root;
        PrintWindow window({root.path(), 30, true});
        window.resize(1060, 720);
        window.show();
        window.findChild<QPushButton*>("navHistory")->click();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* table = window.findChild<QTableView*>("jobsTable");
        auto* fault = window.findChild<QPushButton*>("injectFault");
        auto* area = window.findChild<QScrollArea*>("queueScrollArea");
        auto* toggle = window.findChild<QToolButton*>("logToggle");
        auto* log = window.findChild<QPlainTextEdit*>();
        QVERIFY(table && fault && area && toggle && log);
        QVERIFY(!log->isVisible());
        for (bool showLog : {false, true}) {
            if (showLog) QTest::mouseClick(toggle, Qt::LeftButton);
            QCOMPARE(log->isVisible(), showLog);
            // Regression: at 720 px, the old fixed layout drew buttons over task rows.
            QTRY_VERIFY(table->mapTo(&window, QPoint(0, table->height())).y()
                        <= fault->mapTo(&window, QPoint(0, 0)).y());
            area->ensureWidgetVisible(fault, 0, 0);
            QTRY_VERIFY(area->viewport()->rect().contains(
                QRect(fault->mapTo(area->viewport(), QPoint(0, 0)), fault->size())));
        }
        const auto evidence = qEnvironmentVariable("CPS_GUI_EVIDENCE_DIR");
        if (!evidence.isEmpty()) {
            QVERIFY(QDir().mkpath(evidence));
            QVERIFY(window.grab().save(QDir(evidence).filePath("04-compact-with-logs.png")));
        }
        window.close();
        QTRY_VERIFY(!window.isVisible());
    }
};

QTEST_MAIN(GuiTests)
#include "gui_tests.moc"
