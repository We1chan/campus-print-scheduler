#include "merchant_window.hpp"
#include "coordinator_test_client.hpp"
#include "native_frame_evidence.hpp"

#include <QApplication>
#include <QAction>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFocusEvent>
#include <QEnterEvent>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QScreen>
#include "editorial_chrome.hpp"
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <propkey.h>
#endif
#include <QTimer>
#include <algorithm>
#include <cstdlib>

using namespace cps::gui;
using namespace cps::gui::merchant;

// The static cps_gui library does not automatically retain its resource object.
static void initializeMerchantTestResources() { Q_INIT_RESOURCE(editorial_assets); }

class MerchantTests : public QObject {
    Q_OBJECT

    static int orderIndex(const Snapshot& state, const QString& id) {
        for (int i = 0; i < state.orders.size(); ++i)
            if (state.orders[i].id == id) return i;
        return -1;
    }

    static QSet<int> visibleTableRows(const MerchantWindow& window) {
        QSet<int> rows;
        for (const auto* button : window.findChildren<QPushButton*>()) {
            if (!button->isVisible() || !button->accessibleName().startsWith(QStringLiteral("选择订单 "))) continue;
            bool valid = false;
            const auto index = button->objectName().mid(QStringLiteral("merchant_select-order_").size()).toInt(&valid);
            if (valid) rows.insert(index);
        }
        return rows;
    }

    static bool saveEvidence(QWidget& window, const QString& name) {
        const auto directory = qEnvironmentVariable("CPS_MERCHANT_EVIDENCE_DIR");
        return directory.isEmpty() || (QDir().mkpath(directory) && window.grab().save(QDir(directory).filePath(name)));
    }

    static void setPointerInside(QWidget& widget, bool inside) {
        // Deliver real Qt enter/leave events without moving the shared desktop
        // cursor. Explicit UnderMouse also makes this deterministic offscreen.
        widget.setAttribute(Qt::WA_UnderMouse, inside);
        if (inside) {
            const QPoint center = widget.rect().center();
            QEnterEvent enter(QPointF(center), QPointF(widget.mapTo(widget.window(), center)),
                              QPointF(widget.mapToGlobal(center)));
            QApplication::sendEvent(&widget, &enter);
        } else {
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(&widget, &leave);
        }
        widget.setAttribute(Qt::WA_UnderMouse, inside);
        widget.repaint();
    }

private slots:
    void initTestCase() { initializeMerchantTestResources(); }

    void showcaseBaselineRemainsStationary() {
        MerchantData data({}, true);
        QSignalSpy ready(&data, &MerchantData::backendReady);
        data.start();
        QCOMPARE(ready.size(), 1);
        const auto initial = data.snapshot();
        QVERIFY(initial.showcase && initial.connected);
        QCOMPARE(initial.orders.size(), 48);
        QCOMPARE(initial.todayOrders, 48);
        QCOMPARE(initial.waiting, 6);
        QCOMPARE(initial.printing, 1);
        QCOMPARE(initial.ready, 12);
        QCOMPARE(initial.completed, 29);
        QCOMPARE(initial.revenueCents, qint64(32680));
        QCOMPARE(initial.printedPages, 524);
        QCOMPARE(initial.blackPages, 428);
        QCOMPARE(initial.colorPages, 96);
        QCOMPARE(initial.serviceCounts[0], 12);
        QCOMPARE(initial.serviceCounts[1], 28);
        QCOMPARE(initial.serviceCounts[2], 8);
        QCOMPARE(initial.averageMinutes, 4.2);
        QCOMPARE(initial.devices.size(), 2);
        QCOMPARE(initial.devices[0].printed, 18);
        QCOMPARE(initial.devices[1].queued, 2);
        QTest::qWait(1250); // More than two timer periods: opening a sample never starts it.
        QCOMPARE(data.snapshot().printedPages, initial.printedPages);
        QCOMPARE(data.snapshot().devices[0].printed, initial.devices[0].printed);
        QCOMPARE(data.snapshot().waiting, initial.waiting);
        data.start();
        QCOMPARE(ready.size(), 1); // Start is idempotent.
    }

    void showcaseStartPauseAndComplete() {
        MerchantData data({}, true);
        data.start();
        const int index = orderIndex(data.snapshot(), QStringLiteral("P1024"));
        QVERIFY(index >= 0);
        const int initialPaper = data.snapshot().devices[1].paper;
        data.execute(QStringLiteral("start"), QStringLiteral("1"));
        QTRY_VERIFY_WITH_TIMEOUT(data.snapshot().orders[index].printed > 0, 2000);
        QCOMPARE(data.snapshot().orders[index].state, QStringLiteral("打印中"));
        QCOMPARE(data.snapshot().devices[1].jobId, QStringLiteral("P1024"));
        QCOMPARE(data.snapshot().devices[1].paper, initialPaper - 4); // Eight duplex faces consume four sheets.
        QCOMPARE(data.snapshot().devices[0].printed, 18); // Only the explicitly started device advances.
        data.execute(QStringLiteral("pause"), QStringLiteral("1"));
        const int pausedAt = data.snapshot().orders[index].printed;
        QTest::qWait(1300);
        QCOMPARE(data.snapshot().orders[index].printed, pausedAt);
        QVERIFY(!data.snapshot().devices[1].enabled);
        data.execute(QStringLiteral("start"), QStringLiteral("1"));
        QTRY_COMPARE_WITH_TIMEOUT(data.snapshot().orders[index].state, QStringLiteral("待取件"), 6500);
        QCOMPARE(data.snapshot().orders[index].printed, 8);
        QCOMPARE(data.snapshot().ready, 13);
        data.execute(QStringLiteral("pause"), QStringLiteral("1"));
        data.execute(QStringLiteral("pickup"), QStringLiteral("P1024"));
        QCOMPARE(data.snapshot().orders[index].state, QStringLiteral("已完成"));
        QCOMPARE(data.snapshot().ready, 12);
        QCOMPARE(data.snapshot().completed, 30);
        QCOMPARE(data.snapshot().revenueCents, qint64(33820));
    }

    void showcaseCompatibilityAndQueueOrdering() {
        MerchantData data({}, true);
        QSignalSpy feedback(&data, &MerchantData::feedback);
        const auto colorIndex = orderIndex(data.snapshot(), QStringLiteral("P1024"));
        QVERIFY(colorIndex >= 0);
        data.execute(QStringLiteral("transfer"), QStringLiteral("P1024:0"));
        QCOMPARE(data.snapshot().orders[colorIndex].printer, 1);
        QVERIFY(feedback.last()[1].toBool());
        QVERIFY(feedback.last()[0].toString().contains(QStringLiteral("不支持")));

        data.execute(QStringLiteral("transfer"), QStringLiteral("P1025:1"));
        int moved = orderIndex(data.snapshot(), QStringLiteral("P1025"));
        QCOMPARE(data.snapshot().orders[moved].printer, 1); // A colour device also accepts monochrome work.
        const int beforeMove = moved;
        data.execute(QStringLiteral("reorder"), QStringLiteral("P1025:up"));
        moved = orderIndex(data.snapshot(), QStringLiteral("P1025"));
        QVERIFY(moved < beforeMove);
        QCOMPARE(orderIndex(data.snapshot(), QStringLiteral("P1024")), beforeMove);
        data.execute(QStringLiteral("reorder"), QStringLiteral("P1025:down"));
        QCOMPARE(orderIndex(data.snapshot(), QStringLiteral("P1025")), beforeMove);
        data.execute(QStringLiteral("assign"), QStringLiteral("P1029:0"));
        QCOMPARE(data.snapshot().orders[orderIndex(data.snapshot(), QStringLiteral("P1029"))].printer, 0);
        QCOMPARE(data.snapshot().devices[0].queued, 3);
        QCOMPARE(data.snapshot().devices[1].queued, 3);

        const auto runningIndex = orderIndex(data.snapshot(), QStringLiteral("P1022"));
        data.execute(QStringLiteral("transfer"), QStringLiteral("P1022:1"));
        QCOMPARE(data.snapshot().orders[runningIndex].printer, 0);
        QVERIFY(feedback.last()[1].toBool());
        data.execute(QStringLiteral("cancel"), QStringLiteral("P1029"));
        QCOMPARE(data.snapshot().waiting, 5);
        QCOMPARE(data.snapshot().devices[0].queued, 2);
        data.execute(QStringLiteral("restock"), QStringLiteral("a4:-1"));
        QCOMPARE(data.snapshot().devices[0].paper, 184);
        QVERIFY(feedback.last()[1].toBool());
        data.execute(QStringLiteral("restock"), QStringLiteral("a4:500"));
        QCOMPARE(data.snapshot().devices[0].paper, 684);
        QCOMPARE(data.snapshot().devices[1].paper, 760);

        data.setDevice(1, {{"enabled", false}});
        QVERIFY(!data.snapshot().devices[1].enabled);
        QVERIFY(data.snapshot().devices[1].online); // Pausing automatic work preserves device connectivity.
        QCOMPARE(data.snapshot().devices[1].queued, 3);
        data.setDevice(1, {{"enabled", true}});
        QVERIFY(data.snapshot().devices[1].enabled);
        data.setDevice(0, {{"enabled", false}});
        QVERIFY(data.snapshot().devices[0].enabled); // Configuration changes reject an active device.
        QVERIFY(feedback.last()[1].toBool());
    }

    void customDateRangeRecountsWithoutChangingQueue() {
        MerchantData data({}, true);
        QSignalSpy feedback(&data, &MerchantData::feedback);
        QCOMPARE(data.snapshot().rangeLabel, QStringLiteral("今日"));
        QCOMPARE(data.snapshot().rangeDate, QStringLiteral("2026年10月3日"));
        data.setRange(QStringLiteral("2026-10-04"));
        QCOMPARE(data.snapshot().rangeLabel, QStringLiteral("指定日"));
        QCOMPARE(data.snapshot().rangeDate, QStringLiteral("2026年10月4日"));
        QCOMPARE(data.snapshot().todayOrders, 0);
        QCOMPARE(data.snapshot().revenueCents, qint64(0));
        QCOMPARE(data.snapshot().printedPages, 0);
        QCOMPARE(data.snapshot().serviceCounts[0], 0);
        QCOMPARE(data.snapshot().serviceCounts[1], 0);
        QCOMPARE(data.snapshot().serviceCounts[2], 0);
        QCOMPARE(data.snapshot().waiting, 6);
        QCOMPARE(data.snapshot().printing, 1);
        QCOMPARE(data.snapshot().ready, 12);
        QCOMPARE(data.snapshot().completed, 29);
        QCOMPARE(data.snapshot().devices[0].todayPages, 428); // Device counters retain their daily scope.
        data.setRange(QStringLiteral("2026-02-30"));
        QVERIFY(feedback.last()[1].toBool());
        QCOMPARE(data.snapshot().todayOrders, 0); // An invalid date preserves the chosen range.
        QCOMPARE(data.snapshot().rangeLabel, QStringLiteral("指定日"));
        QCOMPARE(data.snapshot().rangeDate, QStringLiteral("2026年10月4日"));
        data.setRange(QStringLiteral("2026-10-03"));
        QCOMPARE(data.snapshot().rangeDate, QStringLiteral("2026年10月3日"));
        QCOMPARE(data.snapshot().todayOrders, 48);
        QCOMPARE(data.snapshot().revenueCents, qint64(32680));
        QCOMPARE(data.snapshot().printedPages, 524);
        QVERIFY(!feedback.last()[1].toBool());
        data.setRange(QStringLiteral("week"));
        QCOMPARE(data.snapshot().rangeLabel, QStringLiteral("本周"));
        QCOMPARE(data.snapshot().rangeDate, QStringLiteral("2026年10月3日"));
        data.setRange(QStringLiteral("month"));
        QCOMPARE(data.snapshot().rangeLabel, QStringLiteral("本月"));
        QCOMPARE(data.snapshot().rangeDate, QStringLiteral("2026年10月3日"));
        data.setRange(QStringLiteral("today"));
        QCOMPARE(data.snapshot().rangeLabel, QStringLiteral("今日"));
    }

    void showcaseMoveBeforePreservesDeviceBoundaries() {
        MerchantData data({}, true);
        QSignalSpy feedback(&data, &MerchantData::feedback);
        const auto ids = [&data] {
            QStringList result;
            for (const auto& order : data.snapshot().orders) result.append(order.id);
            return result;
        };
        data.execute(QStringLiteral("move-before"), QStringLiteral("P1026:P1025"));
        QCOMPARE(orderIndex(data.snapshot(), QStringLiteral("P1026")) + 1,
                 orderIndex(data.snapshot(), QStringLiteral("P1025")));
        QCOMPARE(data.snapshot().orders[orderIndex(data.snapshot(), QStringLiteral("P1026"))].printer, 0);
        QCOMPARE(data.snapshot().devices[0].queued, 3);
        QCOMPARE(data.snapshot().waiting, 6);
        // Moving forwards also inserts immediately before the target after source removal.
        data.execute(QStringLiteral("move-before"), QStringLiteral("P1026:P1028"));
        QCOMPARE(orderIndex(data.snapshot(), QStringLiteral("P1026")) + 1,
                 orderIndex(data.snapshot(), QStringLiteral("P1028")));
        const auto acceptedOrder = ids();
        data.execute(QStringLiteral("move-before"), QStringLiteral("P1026:P1024"));
        QCOMPARE(ids(), acceptedOrder);
        QVERIFY(feedback.last()[1].toBool()); // A monochrome queue cannot be reordered into the colour queue.
        data.execute(QStringLiteral("move-before"), QStringLiteral("P1022:P1025"));
        QCOMPARE(ids(), acceptedOrder);
        QVERIFY(feedback.last()[1].toBool()); // Running work remains in its current slot.
        data.execute(QStringLiteral("move-before"), QStringLiteral("P1026:missing-order"));
        QCOMPARE(ids(), acceptedOrder);
        QVERIFY(feedback.last()[1].toBool());
        data.execute(QStringLiteral("move-before"), QStringLiteral("P1026:P1026"));
        QCOMPARE(ids(), acceptedOrder);
        QVERIFY(!feedback.last()[1].toBool()); // Dropping a card on itself is harmless.
    }

    void sampleDeviceLimitAndAutomaticTestPage() {
        MerchantData data({}, true);
        QSignalSpy feedback(&data, &MerchantData::feedback);
        data.addSampleDevice(QStringLiteral("   "), true);
        QCOMPARE(data.snapshot().devices.size(), 2);
        QVERIFY(feedback.last()[1].toBool());
        data.addSampleDevice(QStringLiteral("  新增彩色设备  "), true);
        QCOMPARE(data.snapshot().devices.size(), 3);
        QCOMPARE(data.snapshot().devices[2].name, QStringLiteral("新增彩色设备"));
        QCOMPARE(data.snapshot().devices[2].index, 2);
        QVERIFY(data.snapshot().devices[2].color);
        data.execute(QStringLiteral("test-page"), QStringLiteral("2"));
        QCOMPARE(data.snapshot().orders.size(), 49);
        const auto testId = data.snapshot().orders[0].id;
        QVERIFY(testId.startsWith(QStringLiteral("TEST-")));
        QCOMPARE(data.snapshot().orders[0].printer, 2);
        QVERIFY(data.snapshot().orders[0].color);
        // A test-page action starts its own simulation without a separate start command.
        QTRY_COMPARE_WITH_TIMEOUT(data.snapshot().orders[0].state, QStringLiteral("待取件"), 2000);
        QCOMPARE(data.snapshot().orders[0].printed, 1);
        QCOMPARE(data.snapshot().devices[2].paper, 499);
        QCOMPARE(data.snapshot().printedPages, 525);
        QCOMPARE(data.snapshot().devices[0].printed, 18);
        data.setDevice(2, {{"online", false}});
        data.execute(QStringLiteral("test-page"), QStringLiteral("2"));
        QCOMPARE(data.snapshot().orders.size(), 49);
        QVERIFY(feedback.last()[1].toBool());
        for (int index = 3; index < 6; ++index)
            data.addSampleDevice(QStringLiteral("设备 %1").arg(index + 1), false);
        QCOMPARE(data.snapshot().devices.size(), 6);
        data.addSampleDevice(QStringLiteral("第七台"), true);
        QCOMPARE(data.snapshot().devices.size(), 6);
        QVERIFY(feedback.last()[1].toBool());
        QVERIFY(feedback.last()[0].toString().contains(QStringLiteral("六台")));
    }

    void showcaseExportUsesCurrentModel() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        MerchantData data({}, true);
        data.execute(QStringLiteral("cancel"), QStringLiteral("P1029"));
        data.setRange(QStringLiteral("week"));
        const auto path = QDir(directory.path()).filePath(QStringLiteral("营业报表.json"));
        data.exportReport(path);
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonParseError error;
        const auto report = QJsonDocument::fromJson(file.readAll(), &error).object();
        QCOMPARE(error.error, QJsonParseError::NoError);
        QCOMPARE(report.value("mode").toString(), QStringLiteral("merchant_showcase"));
        QVERIFY(report.value("showcase").toBool());
        QCOMPARE(report.value("range").toString(), QStringLiteral("week"));
        QCOMPARE(report.value("revenue_cents").toInt(), 32680);
        QCOMPARE(report.value("printed_pages").toInt(), 524);
        const auto jobs = report.value("jobs").toArray();
        QCOMPARE(jobs.size(), 48);
        bool found = false;
        for (const auto& item : jobs) {
            const auto job = item.toObject();
            if (job.value("job_id").toString() == QStringLiteral("P1029")) {
                found = true;
                QCOMPARE(job.value("state").toString(), QStringLiteral("已取消"));
            }
        }
        QVERIFY(found);
    }

    void nativeNavigationSearchHistoryAndFileDialog() {
        MerchantWindow window({}, true);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* search = window.findChild<QLineEdit*>("merchantSearch");
        QVERIFY(search && search->isVisible());
        QCOMPARE(visibleTableRows(window).size(), 5);
        for (int page = 1; page <= 3; ++page) {
            auto* nav = window.findChild<QPushButton*>(QStringLiteral("merchant_nav_%1").arg(page));
            QVERIFY(nav && nav->isVisible() && !nav->accessibleName().isEmpty());
            QTest::mouseClick(nav, Qt::LeftButton);
            QVERIFY(!search->isVisible());
            if (page == 1) {
                auto* canvas = window.findChild<QWidget*>("merchantCanvas");
                const int source = orderIndex(window.model()->snapshot(), QStringLiteral("P1026"));
                const int destination = orderIndex(window.model()->snapshot(), QStringLiteral("P1025"));
                QVERIFY(canvas && source > destination);
                auto* target = window.findChild<QPushButton*>(QStringLiteral("merchant_select-order_%1").arg(destination));
                QVERIFY(target && target->isVisible());
                const auto point = target->mapTo(canvas, target->rect().center());
                QMimeData mime;
                mime.setData("application/x-paperroom-order", QByteArray("P1026"));
                QDragEnterEvent enter(point, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &enter);
                QVERIFY(enter.isAccepted());
                QDropEvent drop(QPointF(point), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(canvas, &drop);
                QVERIFY(drop.isAccepted());
                QCOMPARE(orderIndex(window.model()->snapshot(), QStringLiteral("P1026")) + 1,
                         orderIndex(window.model()->snapshot(), QStringLiteral("P1025")));
                QCOMPARE(window.model()->snapshot().waiting, 6);
            }
            QVERIFY(saveEvidence(window, QStringLiteral("native-page-%1.png").arg(page)));
        }
        auto* home = window.findChild<QPushButton*>("merchant_nav_0");
        QVERIFY(home);
        QTest::mouseClick(home, Qt::LeftButton);
        QVERIFY(search->isVisible());
        search->setFocus();
        QTest::keyClicks(search, "p1024");
        QCOMPARE(visibleTableRows(window).size(), 1);
        const int selected = *visibleTableRows(window).cbegin();
        QCOMPARE(window.model()->snapshot().orders[selected].id, QStringLiteral("P1024"));
        search->setText(QStringLiteral("不存在的订单"));
        QVERIFY(visibleTableRows(window).isEmpty());
        search->clear();
        auto* complete = window.findChild<QPushButton*>("merchant_filter_3");
        QVERIFY(complete);
        QTest::mouseClick(complete, Qt::LeftButton);
        const auto firstPage = visibleTableRows(window);
        QCOMPARE(firstPage.size(), 5);
        for (const int index : firstPage) QCOMPARE(window.model()->snapshot().orders[index].state, QStringLiteral("已完成"));
        auto* next = window.findChild<QPushButton*>("merchant_table-next_5");
        QVERIFY(next && next->isVisible());
        QTest::mouseClick(next, Qt::LeftButton);
        const auto secondPage = visibleTableRows(window);
        QCOMPARE(secondPage.size(), 5);
        for (const int index : secondPage) QVERIFY(!firstPage.contains(index));
        auto allSeen = firstPage;
        allSeen.unite(secondPage);
        for (int page = 2; page <= 5; ++page) {
            auto* forward = window.findChild<QPushButton*>(QStringLiteral("merchant_table-next_%1").arg(page * 5));
            QVERIFY(forward);
            QTest::mouseClick(forward, Qt::LeftButton);
            allSeen.unite(visibleTableRows(window));
        }
        QCOMPARE(allSeen.size(), 29); // Reach every completed record beyond the reference's first five.
        QVERIFY(!window.findChild<QPushButton*>("merchant_table-next_29"));
        const int historyRow = *visibleTableRows(window).cbegin();
        auto* history = window.findChild<QPushButton*>(QStringLiteral("merchant_select-order_%1").arg(historyRow));
        QVERIFY(history);
        QTest::mouseClick(history, Qt::LeftButton);
        QVERIFY(!window.findChild<QPushButton*>("merchant_start_1")); // Completed records cannot be started again.
        auto* preview = window.findChild<QPushButton*>(QStringLiteral("merchant_view-file_%1").arg(historyRow));
        QVERIFY(preview);
        QTest::mouseClick(preview, Qt::LeftButton);
        QPointer<QDialog> dialog = window.findChild<QDialog*>();
        QVERIFY(dialog && dialog->isVisible());
        bool explainsMetadata = false;
        for (const auto* label : dialog->findChildren<QLabel*>())
            explainsMetadata |= label->text().contains(QStringLiteral("元数据"));
        QVERIFY(explainsMetadata);
        QVERIFY(saveEvidence(*dialog, QStringLiteral("native-file-dialog.png")));
        auto* close = dialog->findChild<QPushButton*>("dialogClose");
        QVERIFY(close);
        QTest::mouseClick(close, Qt::LeftButton);
        QTRY_VERIFY(dialog.isNull());

        window.setPage(0);
        auto* waiting = window.findChild<QPushButton*>("merchant_filter_0");
        QVERIFY(waiting);
        QTest::mouseClick(waiting, Qt::LeftButton);
        const int pendingIndex = orderIndex(window.model()->snapshot(), QStringLiteral("P1024"));
        QVERIFY(pendingIndex >= 0);
        auto* pending = window.findChild<QPushButton*>(QStringLiteral("merchant_select-order_%1").arg(pendingIndex));
        QVERIFY(pending && pending->isVisible());
        QTest::mouseClick(pending, Qt::LeftButton);
        auto* start = window.findChild<QPushButton*>("merchant_start_1");
        QVERIFY(start && start->isVisible());
        QTest::mouseClick(start, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(window.model()->snapshot().devices[1].printed > 0, 2000);
        window.model()->execute(QStringLiteral("pause"), QStringLiteral("1"));
        window.close();
        QVERIFY(!window.isVisible());
    }

    void nativeMerchantShellUsesStorefrontIcon() {
        MerchantWindow window({}, true);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!window.windowIcon().isNull());
        QVERIFY(!window.windowIcon().pixmap(32,32).isNull());
        QVERIFY(window.windowIcon().pixmap(32,32).toImage()!=editorialBrandIcon().pixmap(32,32).toImage());
#ifdef Q_OS_WIN
        if(QGuiApplication::platformName()==QStringLiteral("windows")) {
            IPropertyStore* store=nullptr;
            QVERIFY(SUCCEEDED(SHGetPropertyStoreForWindow(reinterpret_cast<HWND>(window.winId()),IID_PPV_ARGS(&store))));
            auto read=[store](const PROPERTYKEY& key){
                PROPVARIANT value;PropVariantInit(&value);QString text;
                if(SUCCEEDED(store->GetValue(key,&value))&&value.vt==VT_LPWSTR)text=QString::fromWCharArray(value.pwszVal);
                PropVariantClear(&value);return text;
            };
            const QString identity=read(PKEY_AppUserModel_ID);
            const QString command=read(PKEY_AppUserModel_RelaunchCommand);
            const QString icon=read(PKEY_AppUserModel_RelaunchIconResource);
            store->Release();
            QCOMPARE(identity,QStringLiteral("Paperroom.Merchant"));
            QVERIFY(command.contains(QDir::toNativeSeparators(QCoreApplication::applicationFilePath())));
            QVERIFY(icon.endsWith(",0"));
            QVERIFY(QFileInfo::exists(icon.chopped(2)));
            QVERIFY(!QIcon(icon.chopped(2)).isNull());
            if(qEnvironmentVariableIsSet("CPS_MERCHANT_TASKBAR_CAPTURE")) {
                window.raise();window.activateWindow();QTest::qWait(1000);
                auto* screen=window.screen();const QRect desktop=screen->geometry();
                const int barHeight=std::max(1,desktop.bottom()-screen->availableGeometry().bottom());
                const auto strip=screen->grabWindow(0,desktop.x(),desktop.bottom()-barHeight+1,desktop.width(),barHeight);
                QVERIFY(!strip.isNull());
                const auto directory=qEnvironmentVariable("CPS_MERCHANT_EVIDENCE_DIR");
                QVERIFY(!directory.isEmpty()&&QDir().mkpath(directory));
                QVERIFY(strip.save(QDir(directory).filePath("native-taskbar.png")));
            }
        }
#endif
        QVERIFY(saveEvidence(window,QStringLiteral("native-merchant-brand.png")));
        window.close();
    }

    void selectedDeviceImageHoverAndFocusReturnToRest() {
        MerchantWindow window({}, true);
        window.setPage(2);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.raise();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QPushButton* image = nullptr;
        for (auto* button : window.findChildren<QPushButton*>("merchant_select-device_0")) {
            if (button->accessibleName() == window.model()->snapshot().devices[0].name)
                image = button; // The large image hit area, not the separate details link.
        }
        QVERIFY(image && image->isVisible() && image->height() > 100);
        QCOMPARE(image->focusPolicy(), Qt::StrongFocus);
        image->clearFocus();
        setPointerInside(*image, false);
        const QImage rest = image->grab().toImage();
        QVERIFY(!rest.isNull());
        QVERIFY(saveEvidence(window, QStringLiteral("native-device-rest.png")));

        QSignalSpy clicked(image, &QPushButton::clicked);
        QTest::mouseClick(image, Qt::LeftButton);
        QCOMPARE(clicked.size(), 1);
        QVERIFY(image->hasFocus());
        setPointerInside(*image, false);
        // Clicking an already selected device must not latch a focus rectangle
        // over the printer artwork after the pointer has left it.
        QCOMPARE(image->grab().toImage(), rest);
        QFocusEvent focusIn(QEvent::FocusIn, Qt::MouseFocusReason);
        QApplication::sendEvent(image, &focusIn);
        QCOMPARE(image->grab().toImage(), rest);
        image->clearFocus();
        image->setFocus(Qt::TabFocusReason);
        QVERIFY(image->hasFocus());
        QCOMPARE(image->grab().toImage(), rest);
        QVERIFY(saveEvidence(window, QStringLiteral("native-device-focused-no-hover.png")));

        setPointerInside(*image, true);
        QVERIFY(image->underMouse());
        const QImage hovered = image->grab().toImage();
        QCOMPARE(hovered, rest); // Artwork stays unchanged on hover, like student navigation.
        QVERIFY(saveEvidence(window, QStringLiteral("native-device-hover.png")));
        setPointerInside(*image, false);
        QVERIFY(!image->underMouse());
        QVERIFY(image->hasFocus());
        QCOMPARE(image->grab().toImage(), rest);
        QVERIFY(saveEvidence(window, QStringLiteral("native-device-hover-returned.png")));
        window.close();
    }

    void nativeWindowControlsPreserveMouseFocusAndRestore() {
        MerchantWindow window({}, true);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.raise();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        const auto frameError=verifyNativeFrame(window,qEnvironmentVariable("CPS_MERCHANT_EVIDENCE_DIR"),"merchant-native-frame.png");
        QVERIFY2(frameError.isEmpty(),qPrintable(frameError));
        QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));
        auto* maximize = window.findChild<QPushButton*>("windowMaximize");
        auto* minimize = window.findChild<QPushButton*>("windowMinimize");
        auto* close = window.findChild<QPushButton*>("windowClose");
        auto* search = window.findChild<QLineEdit*>("merchantSearch");
        QVERIFY(maximize && minimize && close && search);
        for (const auto* control : {maximize, minimize, close}) {
            QVERIFY(control->isVisible() && !control->accessibleName().isEmpty());
            QCOMPARE(control->focusPolicy(), Qt::TabFocus);
        }
        search->setFocus();
        QVERIFY(search->hasFocus());
        const QSize originalSize = window.size();
        QTest::mouseClick(maximize, Qt::LeftButton);
        QTRY_VERIFY(window.isMaximized());
        QVERIFY(!maximize->hasFocus());
        QVERIFY(search->hasFocus());
        QVERIFY(saveEvidence(window, QStringLiteral("native-window-maximized.png")));
        QTest::mouseClick(maximize, Qt::LeftButton);
        QTRY_VERIFY(!window.isMaximized());
        QTRY_COMPARE(window.size(), originalSize);
        QVERIFY(!maximize->hasFocus());
        QTest::mouseClick(minimize, Qt::LeftButton);
        QTRY_VERIFY(window.isMinimized());
        QVERIFY(!minimize->hasFocus());
        window.showNormal(); // Restore through the OS window state API, as a taskbar does.
        QTRY_VERIFY(!window.isMinimized());
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.raise();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QTRY_COMPARE(window.size(), originalSize);
        QVERIFY(saveEvidence(window, QStringLiteral("native-window-restored.png")));
        maximize->setFocus(Qt::TabFocusReason);
        QTRY_VERIFY(maximize->hasFocus());
        QTest::keyClick(maximize, Qt::Key_Space);
        QTRY_VERIFY(window.isMaximized()); // Tab focus still supports keyboard activation.
        QTest::keyClick(maximize, Qt::Key_Space);
        QTRY_VERIFY(!window.isMaximized());
        QTest::mouseClick(close, Qt::LeftButton);
        QTRY_VERIFY(!window.isVisible());
    }

    void nativeShopMenuKeepsBusinessActionsAndReturns() {
        MerchantWindow window({}, true);
        window.setPage(2);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* shop = window.findChild<QPushButton*>("merchant_shop-menu_");
        auto* search = window.findChild<QLineEdit*>("merchantSearch");
        QVERIFY(shop && search && !search->isVisible());
        auto* queue = window.findChild<QPushButton*>("merchant_nav_1");
        QVERIFY(queue);
        const auto navigationText=[](QPushButton* button){
            const QImage image=button->grab().toImage();
            return image.copy(0,0,image.width(),image.height()*48/56); // Exclude current-page underline.
        };
        const auto unselectedText=navigationText(queue);
        QTest::mouseClick(queue,Qt::LeftButton);
        QCOMPARE(navigationText(queue),unselectedText);
        window.setPage(2);
        setPointerInside(*queue, true);
        QVERIFY(saveEvidence(window, QStringLiteral("native-header-nav-hover.png")));
        setPointerInside(*queue, false);
        setPointerInside(*shop, true);
        QVERIFY(saveEvidence(window, QStringLiteral("native-header-shop-hover.png")));
        setPointerInside(*shop, false);
        QSignalSpy feedback(window.model(), &MerchantData::feedback);
        const QStringList businessActions{QStringLiteral("订单工作台"), QStringLiteral("刷新后台")};
        for (int index = 0; index < businessActions.size(); ++index) {
            bool visible = false, saved = false, activated = false;
            QStringList labels;
            // QMenu::exec enters a nested event loop. Always close its popup,
            // including on a missing action, so a regression cannot hang CI.
            QTimer watchdog;
            watchdog.setSingleShot(true);
            connect(&watchdog, &QTimer::timeout, &window, [] {
                if (auto* popup = QApplication::activePopupWidget()) popup->close();
            });
            watchdog.start(2000);
            QTimer::singleShot(0, &window, [&] {
                auto* menu = window.findChild<QMenu*>("merchantShopMenu");
                if (!menu) return; // The watchdog also handles a renamed popup.
                visible = menu->isVisible();
                QAction* selected = nullptr;
                for (auto* action : menu->actions()) {
                    if (!action->isSeparator()) labels.append(action->text());
                    if (action->text() == businessActions[index]) selected = action;
                }
                saved = saveEvidence(*menu, QStringLiteral("native-shop-menu-%1.png").arg(index));
                if (!visible || !selected || !selected->isEnabled()) { menu->close(); return; }
                connect(selected, &QAction::triggered, &window, [&] { activated = true; });
                QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, menu->actionGeometry(selected).center());
                menu->close();
            });
            QTest::mouseClick(shop, Qt::LeftButton);
            watchdog.stop();
            QVERIFY(visible && saved && activated);
            QCOMPARE(labels, businessActions); // Window controls have their own title-bar buttons.
            QVERIFY(!window.findChild<QMenu*>("merchantShopMenu"));
            QVERIFY(window.isVisible() && !window.isMinimized() && !window.isMaximized());
            QVERIFY(search->isVisible());
        }
        QCOMPARE(feedback.size(), 1);
        QVERIFY(!feedback[0][1].toBool());
        QVERIFY(feedback[0][0].toString().contains(QStringLiteral("刷新")));
        QVERIFY(saveEvidence(window, QStringLiteral("native-shop-menu-returned.png")));
        window.close();
    }

    void persistentBackendReceivesOtherClientOrder() {
        BackendDirectory directory(QDir::tempPath() + QStringLiteral("/店主 IPC-XXXXXX"));
        QVERIFY(directory.isValid());
        const WindowOptions options{directory.path(), 20, false, true};
        MerchantWindow merchant(options); // The ordinary constructor never seeds the showcase.
        merchant.show();
        QSignalSpy ready(&merchant, &MerchantWindow::backendReady);
        QTRY_VERIFY_WITH_TIMEOUT(merchant.model()->snapshot().connected, 10000);
        QVERIFY(!ready.isEmpty());
        QVERIFY(!merchant.model()->snapshot().showcase);
        QCOMPARE(merchant.model()->snapshot().devices.size(), 6);
        QCOMPARE(merchant.model()->snapshot().orders.size(), 0);
        bool menusRounded=false,menusSaved=false;
        QTimer menuWatchdog;
        menuWatchdog.setSingleShot(true);
        connect(&menuWatchdog,&QTimer::timeout,&merchant,[&]{for(auto* menu:merchant.findChildren<QMenu*>())menu->close();});
        menuWatchdog.start(5000);
        QTimer::singleShot(150,&merchant,[&]{
            auto* menu=merchant.findChild<QMenu*>("merchantShopMenu");
            auto* policy=merchant.findChild<QMenu*>("merchantPolicyMenu");
            if(!menu||!policy||!menu->isVisible()){if(menu)menu->close();return;}
            policy->popup(menu->mapToGlobal(QPoint(menu->width()-1,menu->actionGeometry(policy->menuAction()).top())));
            QTest::qWait(50);
            menusRounded=policy->isVisible()&&!menu->mask().isEmpty()&&!policy->mask().isEmpty()
                &&!menu->mask().contains(menu->rect().bottomRight())&&!policy->mask().contains(policy->rect().bottomRight())
                &&menu->windowFlags().testFlag(Qt::NoDropShadowWindowHint)&&policy->windowFlags().testFlag(Qt::NoDropShadowWindowHint);
            menusSaved=saveEvidence(*menu,QStringLiteral("native-shop-rounded-menu.png"))&&saveEvidence(*policy,QStringLiteral("native-policy-rounded-menu.png"));
            policy->close();menu->close();
        });
        auto* shopMenu=merchant.findChild<QPushButton*>("merchant_shop-menu_");
        QVERIFY(shopMenu);
        QTest::mouseClick(shopMenu,Qt::LeftButton);
        menuWatchdog.stop();
        QVERIFY(menusRounded&&menusSaved);
        merchant.setPage(1);
        auto* queuePrevious=merchant.findChild<QPushButton*>("merchant_queue-device-page_-2");
        QVERIFY(queuePrevious&&queuePrevious->isVisible()&&!queuePrevious->isEnabled());
        for(int target:{2,4}){
            auto* next=merchant.findChild<QPushButton*>(QStringLiteral("merchant_queue-device-page_%1").arg(target));
            QVERIFY(next && next->isVisible());
            QTest::mouseClick(next,Qt::LeftButton);
            for(int device:{target,target+1}){
                auto* lane=merchant.findChild<QPushButton*>(QStringLiteral("merchant_select-device_%1").arg(device));
                QVERIFY(lane && lane->isVisible());
            }
        }
        auto* queueNext=merchant.findChild<QPushButton*>("merchant_queue-device-page_6");
        QVERIFY(queueNext&&queueNext->isVisible()&&!queueNext->isEnabled());
        QVERIFY(saveEvidence(merchant,QStringLiteral("native-queue-last-device-page.png")));
        for(int target:{2,0}){
            auto* previous=merchant.findChild<QPushButton*>(QStringLiteral("merchant_queue-device-page_%1").arg(target));
            QVERIFY(previous && previous->isVisible());
            QTest::mouseClick(previous,Qt::LeftButton);
        }
        QVERIFY(saveEvidence(merchant,QStringLiteral("native-queue-first-device-page.png")));
        merchant.setPage(0);
        QSignalSpy navigationFeedback(merchant.model(),&MerchantData::feedback);
        auto* allDevices=merchant.findChild<QPushButton*>("merchant_page_devices");
        QVERIFY(allDevices&&allDevices->isVisible()&&allDevices->isEnabled());
        QTest::mouseClick(allDevices,Qt::LeftButton);
        QVERIFY(!merchant.findChild<QLineEdit*>("merchantSearch")->isVisible());
        QCOMPARE(navigationFeedback.size(),0);
        auto* previousDevice=merchant.findChild<QPushButton*>("merchant_device-prev_");
        auto* nextDevice=merchant.findChild<QPushButton*>("merchant_device-next_");
        auto* addDevice=merchant.findChild<QPushButton*>("merchant_add-device_");
        auto* canvas=merchant.findChild<QWidget*>("merchantCanvas");
        QVERIFY(previousDevice&&nextDevice&&addDevice&&canvas);
        const QRect previousGeometry=previousDevice->geometry(),nextGeometry=nextDevice->geometry();
        QCOMPARE(previousDevice->size(),addDevice->size());
        QCOMPARE(nextDevice->size(),addDevice->size());
        QCOMPARE(addDevice->accessibleName(),QStringLiteral("添加设备"));
        QVERIFY(previousDevice->isVisible()&&!previousDevice->isEnabled());
        QVERIFY(nextDevice->isEnabled());
        QTest::mouseClick(previousDevice,Qt::LeftButton);
        QVERIFY(canvas->accessibleDescription().isEmpty());
        QCOMPARE(navigationFeedback.size(),0);
        for(int target:{2,4}){
            QTest::mouseClick(nextDevice,Qt::LeftButton);
            for(int device:{target,target+1}){
                auto* card=merchant.findChild<QPushButton*>(QStringLiteral("merchant_select-device_%1").arg(device));
                QVERIFY(card&&card->isVisible());
            }
            QCOMPARE(previousDevice->geometry(),previousGeometry);
            QCOMPARE(nextDevice->geometry(),nextGeometry);
            QVERIFY(previousDevice->isEnabled());
        }
        QVERIFY(!nextDevice->isEnabled());
        QTest::mouseClick(nextDevice,Qt::LeftButton);
        QVERIFY(canvas->accessibleDescription().isEmpty());
        QCOMPARE(navigationFeedback.size(),0);
        QVERIFY(previousDevice->isVisible()&&nextDevice->isVisible());
        QVERIFY(saveEvidence(merchant,QStringLiteral("native-devices-last-page.png")));
        for(int target:{2,0}){
            QTest::mouseClick(previousDevice,Qt::LeftButton);
            QVERIFY(merchant.findChild<QPushButton*>(QStringLiteral("merchant_select-device_%1").arg(target)));
        }
        QVERIFY(!previousDevice->isEnabled()&&nextDevice->isEnabled());
        QVERIFY(saveEvidence(merchant,QStringLiteral("native-devices-first-page.png")));
        merchant.setPage(0);
        merchant.model()->execute(QStringLiteral("pause-all"));
        QTRY_VERIFY(merchant.model()->snapshot().paused);

        CoordinatorClient audit(directory.path());
        merchant.model()->setDevice(0, {{"enabled", false}});
        QTRY_VERIFY(!merchant.model()->snapshot().devices[0].online);
        QVERIFY(!merchant.model()->snapshot().devices[0].enabled);
        const auto disabledFleet = audit.request(QStringLiteral("snapshot")).value("fleet").toArray();
        QCOMPARE(disabledFleet.size(), 6);
        QVERIFY(!disabledFleet[0].toObject().value("online").toBool());
        QVERIFY(!disabledFleet[0].toObject().contains("enabled")); // IPC uses its existing online capability field.
        merchant.model()->setDevice(0, {{"enabled", true}});
        QTRY_VERIFY(merchant.model()->snapshot().devices[0].online);
        QVERIFY(merchant.model()->snapshot().devices[0].enabled);

        PrintController student(options);
        QSignalSpy submitted(&student, &PrintController::orderResult);
        student.initialize();
        const QJsonObject order{{"title", QStringLiteral("跨客户端真实订单.pdf")}, {"pages", 3}, {"copies", 2},
            {"paper", "a4"}, {"color", true}, {"duplex", false}, {"service", "standard"},
            {"customer", "2024000001"}, {"payment", "wechat"}};
        student.submitOrder(order);
        QCOMPARE(submitted.size(), 1);
        QVERIFY2(submitted[0][0].toBool(), qPrintable(submitted[0][2].toString()));
        const auto id = submitted[0][1].toString();
        QVERIFY(!id.isEmpty());
        QTRY_COMPARE(merchant.model()->snapshot().orders.size(), 1);
        const auto queued = merchant.model()->snapshot().orders[0];
        QCOMPARE(queued.id, id);
        QCOMPARE(queued.title, order.value("title").toString());
        QCOMPARE(queued.student, QStringLiteral("2024000001"));
        QCOMPARE(queued.pages, 3);
        QCOMPARE(queued.copies, 2);
        QVERIFY(queued.color && !queued.duplex);
        QCOMPARE(queued.state, QStringLiteral("待打印"));
        QCOMPARE(queued.estimatedMinutes, -1); // A paused queue cannot promise completion from the current time.
        QCOMPARE(queued.printer, -1);
        QCOMPARE(queued.amountCents, qint64(855)); // Six colour faces at 150 cents with the standard 95% rate.
        QVERIFY(queued.path.isEmpty()); // IPC supplies metadata, never fabricated uploaded bytes.
        QCOMPARE(merchant.model()->snapshot().waiting, 1);
        QCOMPARE(merchant.model()->snapshot().printedPages, 0);
        QCOMPARE(merchant.model()->snapshot().revenueCents, qint64(0));
        const auto raw = audit.request(QStringLiteral("snapshot")).value("jobs").toArray();
        QCOMPARE(raw.size(), 1);
        QCOMPARE(raw[0].toObject().value("job_id").toString(), id);
        QCOMPARE(raw[0].toObject().value("payment").toString(), QStringLiteral("wechat"));
        QCOMPARE(raw[0].toObject().value("base_cents").toInt(), 900);
        QCOMPARE(raw[0].toObject().value("quoted_cents").toInt(), 855);

        merchant.model()->execute(QStringLiteral("resume-all"));
        QTRY_COMPARE_WITH_TIMEOUT(merchant.model()->snapshot().orders[0].state, QStringLiteral("已完成"), 10000);
        QCOMPARE(merchant.model()->snapshot().orders[0].estimatedMinutes, 0);
        QCOMPARE(merchant.model()->snapshot().orders[0].printed, 6);
        QCOMPARE(merchant.model()->snapshot().completed, 1);
        QCOMPARE(merchant.model()->snapshot().colorPages, 6);
        QCOMPARE(merchant.model()->snapshot().revenueCents, qint64(855));
        const int printer = merchant.model()->snapshot().orders[0].printer;
        QVERIFY(printer >= 0 && printer < 6);
        QVERIFY(merchant.model()->snapshot().devices[printer].color);
        QVERIFY(QFileInfo::exists(QDir(directory.path()).filePath("output/" + id + ".txt")));
        student.requestClose();
        merchant.close();
        QVERIFY(!merchant.isVisible());
        // The directory destructor sends shutdown only to this unique test endpoint.
    }
};

QTEST_MAIN(MerchantTests)
#include "merchant_tests.moc"
