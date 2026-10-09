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
        bool restored=false;
        void apply() {
            if(restored)return;
            restored=true;
            SetWindowPos(hwnd,topmost?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
            if(!window.isVisible())window.show();
            window.resize(size);window.move(position);
            window.raise();window.activateWindow();
        }
        ~RestoreWindow() { apply(); }
    } restore{window,hwnd,originalSize,originalPosition,
              bool(GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_TOPMOST)};
    window.resize(window.minimumSize().expandedTo(QSize(920,640)));
    QTest::qWait(100); // Let native resize/layout notifications settle first.
    QWidget backdrop;
    backdrop.setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    backdrop.setStyleSheet("background:#B8C1CC;");
    QRect available=window.screen()->availableGeometry();
    // Reproduce a hosted desktop's small capture area without changing the
    // user's display resolution. This bounds only our own opaque backdrop.
    const auto canvasSize=qEnvironmentVariable("CPS_GUI_FRAME_CANVAS_SIZE").split('x');
    if(canvasSize.size()==2) {
        const QSize bounded(canvasSize[0].toInt(),canvasSize[1].toInt());
        if(!bounded.isValid()||bounded.isEmpty())return "Invalid native frame canvas size";
        available.setSize(available.size().boundedTo(bounded));
    }
    backdrop.setGeometry(available);
    backdrop.show();if(!QTest::qWaitForWindowExposed(&backdrop))return "Frame backdrop did not expose";
    const QRect frame=window.frameGeometry();
    window.move(window.pos()+available.center()-frame.center());
    SetWindowPos(hwnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE);
    window.raise();window.activateWindow();
    if(!QTest::qWaitForWindowActive(&window))return "Frame evidence window did not activate";
    const auto captureShadow=[&](QWidget& target,const QString& filename,bool& visible)->QString {
        QTest::qWait(250);DwmFlush();
        const QRect outer=target.frameGeometry();
        const QRect capture=outer.adjusted(-40,-40,40,40).intersected(backdrop.geometry()).intersected(available);
        // Capture only our opaque backdrop, never unrelated desktop content.
        const QRect sample(outer.left()+40,outer.bottom()+5,outer.width()-80,1);
        if(capture.isEmpty()||!capture.contains(sample))
            return QString("Native shadow sample does not fit: frame=%1,%2 %3x%4; canvas=%5,%6 %7x%8")
                .arg(outer.x()).arg(outer.y()).arg(outer.width()).arg(outer.height())
                .arg(available.x()).arg(available.y()).arg(available.width()).arg(available.height());
        const auto image=target.screen()->grabWindow(0,capture.x(),capture.y(),capture.width(),capture.height()).toImage();
        if(image.isNull())return "Native frame screenshot is empty";
        const qreal scale=image.devicePixelRatio();
        const int y=qRound((sample.top()-capture.top())*scale);
        if(y>=image.height())return "Native frame shadow sample is outside the screenshot";
        int shaded=0;
        const int left=qRound((sample.left()-capture.left())*scale);
        const int right=qRound((sample.left()+sample.width()-capture.left())*scale);
        for(int x=left;x<right;++x){
            const QColor pixel=image.pixelColor(x,y);
            if(pixel.red()<181&&pixel.green()<190&&pixel.blue()<201)++shaded;
        }
        visible=shaded>=(right-left)/2;
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
    reference.close();backdrop.hide();restore.apply();
    if(!QTest::qWaitForWindowActive(&window))return "Frame evidence window did not reactivate";
    QTest::qWait(100); // Restore layout before the caller clicks its controls.
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

