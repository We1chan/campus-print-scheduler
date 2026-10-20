#pragma once
#include "merchant_data.hpp"
#include "merchant_painter.hpp"
#include <QMainWindow>
#include <QLineEdit>
#include <QPushButton>
#include <QHash>
namespace cps::gui {
QIcon merchantBrandIcon();
class MerchantCanvas;
class MerchantWindow : public QMainWindow {
    Q_OBJECT
public:
    MerchantWindow(WindowOptions options,bool showcase=false);
    void setPage(int index);
    void openDispatchWorkspace();
    void captureAll(const QString& directory);
    merchant::MerchantData* model() const {return model_;}
signals:
    void backendReady();
private:
    void action(const QString& name,const QString& value);
    void notify(const QString& message,bool problem=false);
    void viewFile();
    void selectDevice(int index);
    void pageDevices(bool next);
    void selectRange(const QString& label);
    void exportReport();
    void showOrderMenu(int index);
    void showShopMenu();
    void openDeviceSettings(bool adding,const QString& value);
    void openRestock();
    void chooseSampleDevice(const QString& name,const QString& value);
    merchant::MerchantData* model_;
    MerchantCanvas* canvas_;
    std::array<int,4> deviceSelections_{{1,0,0,0}};
};
}
