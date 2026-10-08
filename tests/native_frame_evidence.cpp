#include "native_frame_evidence.hpp"
#include <QApplication>
#include <QDir>
#include <QScreen>
#include <QTest>
#include <QWidget>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

QString verifyNativeFrame(QWidget& window, const QString& evidenceDirectory, const QString& name) {
#ifdef Q_OS_WIN
    if(QGuiApplication::platformName()!=QStringLiteral("windows"))return {};
    const auto hwnd=reinterpret_cast<HWND>(window.winId());
    const auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);
    if(!(style&WS_CAPTION))return "Missing native frame style";
    BOOL rendering=FALSE;
    if(FAILED(DwmGetWindowAttribute(hwnd,DWMWA_NCRENDERING_ENABLED,&rendering,sizeof(rendering)))||!rendering)
        return "Native frame rendering is disabled";
    DWORD corner=0;
    if(SUCCEEDED(DwmGetWindowAttribute(hwnd,33,&corner,sizeof(corner)))&&corner!=2)
        return "Windows corner preference was not applied";
    if(evidenceDirectory.isEmpty())return {};
    const QSize originalSize=window.size();const QPoint originalPosition=window.pos();
    window.resize(window.minimumSize().expandedTo(QSize(920,640)));
    QWidget backdrop;
    backdrop.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    backdrop.setStyleSheet("background:#B8C1CC;");
    const QSize canvas=window.size()+QSize(120,120);
    const QRect available=window.screen()->availableGeometry();
    backdrop.resize(canvas);backdrop.move(available.center()-QPoint(canvas.width()/2,canvas.height()/2));
    backdrop.show();if(!QTest::qWaitForWindowExposed(&backdrop))return "Frame backdrop did not expose";
    window.move(backdrop.pos()+QPoint(60,60));
    SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
    window.raise();window.activateWindow();
    if(!QTest::qWaitForWindowActive(&window))return "Frame evidence window did not activate";
    QTest::qWait(250);DwmFlush();
    const QRect capture(backdrop.pos()+QPoint(20,20),window.size()+QSize(80,80));
    const auto image=window.screen()->grabWindow(0,capture.x(),capture.y(),capture.width(),capture.height()).toImage();
    const qreal scale=image.devicePixelRatio();
    const int y=qRound((40+window.height()+4)*scale);
    int shaded=0;
    for(int x=qRound(80*scale);x<qRound((window.width())*scale);++x){
        const QColor pixel=image.pixelColor(x,y);
        if(pixel.red()<181&&pixel.green()<190&&pixel.blue()<201)++shaded;
    }
    const bool saved=QDir().mkpath(evidenceDirectory)&&image.save(QDir(evidenceDirectory).filePath(name));
    backdrop.close();
    SetWindowPos(hwnd,HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
    window.resize(originalSize);window.move(originalPosition);
    if(!saved)return "Cannot save native frame screenshot";
    if(shaded<window.width()*scale/2)return "No compositor shadow beneath the window";
#else
    Q_UNUSED(window);Q_UNUSED(evidenceDirectory);Q_UNUSED(name);
#endif
    return {};
}

