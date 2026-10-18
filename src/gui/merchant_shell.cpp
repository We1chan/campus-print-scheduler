#include "merchant_shell.hpp"
#include "merchant_window.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#endif

namespace cps::gui {
void configureMerchantProcess() {
    QApplication::setWindowIcon(merchantBrandIcon());
    QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei UI"),10));
#ifdef Q_OS_WIN
    if(FAILED(SetCurrentProcessExplicitAppUserModelID(L"Paperroom.Merchant")))
        qWarning("Could not set the merchant taskbar identity");
#endif
}

#ifdef Q_OS_WIN
static QString quoteWindowsArgument(const QString& argument) {
    QString quoted="\"";int slashes=0;
    for(QChar character:argument){
        if(character=='\\'){++slashes;continue;}
        quoted+=QString(slashes*(character=='"'?2:1)+(character=='"'?1:0),'\\');
        quoted+=character;slashes=0;
    }
    return quoted+QString(slashes*2,'\\')+'"';
}
#endif

void configureMerchantTaskbar(QWidget* window) {
#ifdef Q_OS_WIN
    if(QGuiApplication::platformName()!=QStringLiteral("windows"))return;
    IPropertyStore* store=nullptr;
    if(FAILED(SHGetPropertyStoreForWindow(reinterpret_cast<HWND>(window->winId()),IID_PPV_ARGS(&store))))return;
    auto set=[store](const PROPERTYKEY& key,const QString& text){
        PROPVARIANT value;PropVariantInit(&value);
        if(SUCCEEDED(InitPropVariantFromString(reinterpret_cast<LPCWSTR>(text.utf16()),&value))){
            const HRESULT result=store->SetValue(key,value);
            if(FAILED(result))qWarning("Could not set merchant taskbar property %lu (0x%08lx)",static_cast<unsigned long>(key.pid),static_cast<unsigned long>(result));
            PropVariantClear(&value);
        }
    };
    // Persist the shop launch settings, excluding one-shot diagnostics and test arguments.
    const QStringList current=QCoreApplication::arguments();
    QStringList arguments{QDir::toNativeSeparators(QCoreApplication::applicationFilePath())};
    for(int i=1;i<current.size();++i){
        if(current[i]=="--merchant-showcase")arguments<<current[i];
        else if((current[i]=="--data-dir"||current[i]=="--page-ms")&&i+1<current.size()){
            arguments<<current[i]<<current[i+1];++i;
        }else if(current[i].startsWith("--data-dir=")||current[i].startsWith("--page-ms="))arguments<<current[i];
    }
    for(auto& argument:arguments)argument=quoteWindowsArgument(argument);
    set(PKEY_AppUserModel_ID,QStringLiteral("Paperroom.Merchant"));
    set(PKEY_AppUserModel_RelaunchCommand,arguments.join(' '));
    set(PKEY_AppUserModel_RelaunchDisplayNameResource,QStringLiteral("纸间 · 店主工作台"));
    const QDir app(QCoreApplication::applicationDirPath());
    const QString icon=QFileInfo::exists(app.filePath("paperroom-merchant-v4.ico"))?app.filePath("paperroom-merchant-v4.ico"):app.filePath("paperroom-merchant-v3.ico");
    set(PKEY_AppUserModel_RelaunchIconResource,QDir::toNativeSeparators(QFileInfo::exists(icon)?icon:QCoreApplication::applicationFilePath())+",0");
    if(QFileInfo::exists(icon)) {
        const auto hwnd=reinterpret_cast<HWND>(window->winId());
        for(const auto& size:{std::pair<int,int>{ICON_BIG,32},std::pair<int,int>{ICON_SMALL,16}}) {
            const auto handle=static_cast<HICON>(LoadImageW(nullptr,reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(icon).utf16()),IMAGE_ICON,size.second,size.second,LR_LOADFROMFILE));
            if(handle){SendMessageW(hwnd,WM_SETICON,size.first,reinterpret_cast<LPARAM>(handle));QObject::connect(window,&QObject::destroyed,[handle]{DestroyIcon(handle);});}
        }
    }
    if(FAILED(store->Commit()))qWarning("Could not commit merchant taskbar properties");
    store->Release();
#else
    Q_UNUSED(window);
#endif
}
}
