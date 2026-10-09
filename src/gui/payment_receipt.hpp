#pragma once
#include "payment_icon.hpp"
#include "editorial_chrome.hpp"
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QVBoxLayout>

namespace cps::gui {
class ReceiptCodePanel : public QFrame {
public:
    explicit ReceiptCodePanel(const QString &directory, bool includeDefaults = false)
        : directory_(directory), includeDefaults_(includeDefaults) {
        setObjectName("paymentReceipt");
        setStyleSheet("QFrame#paymentReceipt{background:#F3F1EC;border:1px solid #E8E3DA;border-radius:12px;}"
                     "QLabel{background:transparent;border:none;color:#14213E;font-size:13px;}");
        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(12, 12, 16, 12);
        row->setSpacing(18);
        code_ = new QLabel;
        code_->setObjectName("paymentReceiptCode");
        code_->setFixedSize(132, 132);
        code_->setAlignment(Qt::AlignCenter);
        code_->setStyleSheet("background:white;border:1px solid #E6E1D9;border-radius:8px;");
        row->addWidget(code_);
        auto *details = new QVBoxLayout;
        details->setSpacing(8);
        title_ = new QLabel;
        title_->setObjectName("paymentReceiptTitle");
        title_->setStyleSheet("font-size:16px;font-weight:600;");
        title_->setWordWrap(true);
        hint_ = new QLabel;
        hint_->setObjectName("paymentReceiptHint");
        hint_->setWordWrap(true);
        hint_->setStyleSheet("color:#777C86;font-size:12px;");
        details->addStretch();
        details->addWidget(title_);
        details->addWidget(hint_);
        auto *enlarge = new QPushButton(QStringLiteral("放大收款码"));
        enlarge_ = enlarge;
        enlarge->setObjectName("paymentReceiptEnlarge");
        enlarge->setAutoDefault(false);
        details->addWidget(enlarge, 0, Qt::AlignLeft);
        connect(enlarge, &QPushButton::clicked, this, [this] { showCode(); });
        details->addStretch();
        row->addLayout(details, 1);
    }
    void setMethod(const QString &method) {
        setProperty("method", method);
        const bool card = method == "campus_card";
        const QString brand = method == "wechat" ? QStringLiteral("微信") : QStringLiteral("支付宝");
        title_->setText(card ? QStringLiteral("校园卡支付") : brand + QStringLiteral("收款码"));
        code_->setAccessibleName(title_->text());
        QString path = QDir(directory_).filePath(method + ".png");
        if (!QFileInfo::exists(path)) path = QDir(directory_).filePath(method + ".jpg");
        // Explicit local files take precedence, including corrupt files. Never
        // hide a bad shop configuration behind another recipient's bundled code.
        if (!card && includeDefaults_ && !QFileInfo::exists(path))
            path = ":/editorial/payment-codes/" + method + ".jpg";
        code_->setProperty("sourcePath", card ? QString() : path);
        QImage image;
        if (!card && QFileInfo(path).size() <= 8 * 1024 * 1024) {
            QImageReader reader(path);
            const QSize size = reader.size();
            if (size.isValid() && size.width() <= 4096 && size.height() <= 4096)
                image = reader.read();
        }
        code_->setProperty("configured", !image.isNull());
        image_ = image;
        enlarge_->setVisible(!image.isNull());
        if (!image.isNull()) {
            // Keep the entire supplied image and its quiet zone visible.
            code_->setPixmap(QPixmap::fromImage(image).scaled(116, 116, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            hint_->setText(QStringLiteral("点击放大查看%1收款码。\n扫码会真实转账；应用内确认仅为模拟，尚不自动确认到账。").arg(brand));
        } else {
            QPixmap placeholder(132, 132);
            placeholder.fill(Qt::transparent);
            QPainter p(&placeholder);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor("#DBD5CA"), 2));
            for (const QPoint &corner : {QPoint(17, 17), QPoint(115, 17), QPoint(17, 115), QPoint(115, 115)}) {
                const int dx = corner.x() < 66 ? 12 : -12;
                const int dy = corner.y() < 66 ? 12 : -12;
                p.drawLine(corner, corner + QPoint(dx, 0));
                p.drawLine(corner, corner + QPoint(0, dy));
            }
            paymentIcon(method).paint(&p, QRect(49, 36, 34, 34));
            p.setPen(QColor("#8A857D"));
            QFont f("Microsoft YaHei"); f.setPixelSize(12); p.setFont(f);
            p.drawText(QRect(10, 78, 112, 25), Qt::AlignCenter,
                       card ? QStringLiteral("刷卡区预留") : QStringLiteral("收款码待配置"));
            p.end();
            code_->setPixmap(placeholder);
            hint_->setText(card ? QStringLiteral("校园卡终端接入位置。\n本次演示可直接确认支付。")
                                : QStringLiteral("此处预留%1收款码。\n配置图片后会在这里展示。").arg(brand));
        }
    }
private:
    void showCode() {
        if (image_.isNull()) return;
        auto *dialog = new EditorialDialog(window(), false);
        dialog->setObjectName("receiptCodeDialog");
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowModality(Qt::WindowModal);
        dialog->setWindowTitle(title_->text());
        dialog->setWindowIcon(editorialBrandIcon());
        const QRect available = screen()->availableGeometry();
        const QSize imageSpace(qMin(560, available.width() - 64), qMin(720, available.height() - 184));
        const QPixmap image = QPixmap::fromImage(image_).scaled(imageSpace, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(0, 0, 0, 16);
        layout->addWidget(editorialDialogHeader(dialog, title_->text(), {}, false));
        auto *picture = new QLabel;
        picture->setObjectName("receiptCodeFullImage");
        picture->setAlignment(Qt::AlignCenter);
        picture->setPixmap(image);
        layout->addWidget(picture, 0, Qt::AlignCenter);
        auto *note = new QLabel(QStringLiteral("扫码会真实转账。系统尚不自动确认到账，应用内确认仅为模拟。"));
        note->setWordWrap(true);
        note->setProperty("role", "muted");
        note->setContentsMargins(20, 4, 20, 0);
        layout->addWidget(note);
        dialog->setFixedSize(qMax(400, image.width() + 40), image.height() + 136);
        dialog->move(available.center() - dialog->rect().center());
        dialog->show();
    }
    QString directory_;
    bool includeDefaults_ = false;
    QLabel *code_ = nullptr, *title_ = nullptr, *hint_ = nullptr;
    QPushButton *enlarge_ = nullptr;
    QImage image_;
};
} // namespace cps::gui
