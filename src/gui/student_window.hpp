#pragma once
#include "print_controller.hpp"
#include <QDialog>
#include <QMainWindow>
#include <QPointer>
#include <QThread>
#include <array>
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTimer;
class QBoxLayout;

namespace cps::gui {
class PrintProof;
struct DocumentInfo {
    QString name;
    qint64 bytes = 0;
    int pages = 0;  // 0: could not be detected, the student enters it
    QString error;
};
// Reads metadata only. The simulated shop registers the name and page count and
// never stores or transfers document contents.
DocumentInfo inspectDocument(const QString& path);

// Self-service client: upload, options, quote with estimated wait, simulated
// payment, and the student's own orders. Like the owner console it only holds
// coordinator snapshots and sends commands.
class StudentWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit StudentWindow(WindowOptions options, const QString& studentId = {}, QWidget* parent = nullptr);
    ~StudentWindow() override;
    void setDocument(const QString& path);
signals:
    void backendReady();
    void quoteRequested(QJsonObject order, int token);
    void orderRequested(QJsonObject order);
    void cancelRequested(const QString& id);
    void closeRequested();
protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    QJsonObject order() const;
    QString student() const;
    void updatePrice();
    void applyQuote(int token, const QJsonObject& quote);
    void applyView(const ViewState& view);
    void applyOrderResult(bool accepted, const QString& id, const QString& message);
    void updateActions();
    void clearDocument();
    void updateProof();
    void showPayment();
    QThread backendThread_;
    PrintController* controller_ = nullptr;
    ViewState view_;
    QStackedWidget* pages_ = nullptr;
    PrintProof* proof_ = nullptr;
    QPushButton* flipProof_ = nullptr;
    QBoxLayout* orderColumns_ = nullptr;
    QBoxLayout* settingsDock_ = nullptr;
    QLineEdit* student_ = nullptr;
    QFrame* drop_ = nullptr;
    QLabel* fileName_ = nullptr;
    QLabel* file_ = nullptr;
    QPushButton* choose_ = nullptr;
    QPushButton* remove_ = nullptr;
    QLineEdit* title_ = nullptr;
    QSpinBox* pageCount_ = nullptr;
    QSpinBox* copies_ = nullptr;
    QComboBox* paper_ = nullptr;
    QCheckBox* color_ = nullptr;
    QCheckBox* duplex_ = nullptr;
    QButtonGroup* service_ = nullptr;
    QButtonGroup* payment_ = nullptr;
    QLabel* price_ = nullptr;
    QLabel* priceDetail_ = nullptr;
    QLabel* eta_ = nullptr;
    QLabel* deadline_ = nullptr;
    QPushButton* pay_ = nullptr;
    QLabel* payHint_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QTableWidget* orders_ = nullptr;
    QTableWidget* devices_ = nullptr;
    QTableWidget* queue_ = nullptr;
    QWidget* ordersEmpty_ = nullptr;
    QWidget* queueEmpty_ = nullptr;
    QLabel* ordersSummary_ = nullptr;
    QLabel* feedback_ = nullptr;
    QLabel* connection_ = nullptr;
    std::array<QLabel*, 4> metricValues_{};
    std::array<QLabel*, 4> metricHints_{};
    QTimer* quoteDelay_ = nullptr;
    QPointer<QDialog> paymentDialog_;
    QStringList orderIds_;
    QString successMessage_;
    QString pendingSelect_;  // order to select in "我的订单" once a snapshot lists it
    Money quotedCents_ = 0;
    int quoteToken_ = 0;
    bool documentChosen_ = false;
    bool quoteAccepted_ = false;
    bool quotePending_ = true;
    bool closeApproved_ = false;
    bool closePending_ = false;
};
} // namespace cps::gui
