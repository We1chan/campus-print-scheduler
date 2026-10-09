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
    struct RestoreWindow {
        QWidget& window; HWND hwnd; QSize size; QPoint position; bool topmost;
        ~RestoreWindow() {
            SetWindowPos(hwnd,topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
            if(!window.isVisible())window.show();
            window.resize(size);window.move(position);
        }
    } restore{window,hwnd,originalSize,originalPosition,
              bool(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_TOPMOST)};
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
    const auto captureShadow=[&](QWidget& target,const QString& filename,bool& visible)->QString {
        QTest::qWait(250);DwmFlush();
        const QRect outer=target.frameGeometry();
        const QRect capture=outer.adjusted(-40,-40,40,40);
        // Capture only our opaque backdrop, never unrelated desktop content.
        if(!backdrop.geometry().contains(capture)||!available.contains(capture))
            return "Native frame capture does not fit the controlled backdrop";
        const auto image=target.screen()->grabWindow(0,capture.x(),capture.y(),capture.width(),capture.height()).toImage();
        if(image.isNull())return "Native frame screenshot is empty";
        const qreal scale=image.devicePixelRatio();
        const int y=qRound((40+outer.height()+4)*scale);
        if(y>=image.height())return "Native frame shadow sample is outside the screenshot";
        int shaded=0;
        for(int x=qRound(80*scale);x<qRound(outer.width()*scale);++x){
            const QColor pixel=image.pixelColor(x,y);
            if(pixel.red()<181&&pixel.green()<190&&pixel.blue()<201)++shaded;
        }
        visible=shaded>=outer.width()*scale/2;
        if(!QDir().mkpath(evidenceDirectory)||!image.save(QDir(evidenceDirectory).filePath(filename)))
            return "Cannot save native frame screenshot";
        return {};
    };
    bool applicationShadow=false;
    const auto applicationError=captureShadow(window,name,applicationShadow);
    if(!applicationError.isEmpty())return applicationError;

    // Windows Server / virtual desktops can suppress DWM shadows even while
    // non-client rendering is enabled. A normal system window is the control:
    // never blame the app for an effect the compositor cannot render at all.
    window.hide();
    QWidget reference;
    reference.setWindowTitle("Native compositor control");
    reference.resize(400,180);
    reference.move(backdrop.geometry().center()-QPoint(200,90));
    reference.show();
    if(!QTest::qWaitForWindowExposed(&reference))return "Native compositor control did not expose";
    SetWindowPos(reinterpret_cast<HWND>(reference.winId()),HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
    reference.raise();reference.activateWindow();
    if(!QTest::qWaitForWindowActive(&reference))return "Native compositor control did not activate";
    bool referenceShadow=false;
    const auto referenceError=captureShadow(reference,name+"-control.png",referenceShadow);
    reference.close();window.show();
    if(!referenceError.isEmpty())return referenceError;
    qInfo("Native shadow pixels: application=%d, system_control=%d",int(applicationShadow),int(referenceShadow));
    if(applicationShadow)return {};
    if(referenceShadow)return "No compositor shadow beneath the application (system control has a shadow)";
    if(qEnvironmentVariableIntValue("CPS_GUI_REQUIRE_NATIVE_SHADOW"))
        return "Native shadows are unavailable on this desktop (strict visual acceptance requested)";
    BOOL enabled=FALSE;SystemParametersInfoW(SPI_GETDROPSHADOW,0,&enabled,0);
    qInfo("Native frame configuration verified; shadow pixels unavailable on this desktop: "
          "application and system control both have no shadow (SPI_GETDROPSHADOW=%d)",int(enabled));
#else
    Q_UNUSED(window);Q_UNUSED(evidenceDirectory);Q_UNUSED(name);
#endif
    return {};
}

