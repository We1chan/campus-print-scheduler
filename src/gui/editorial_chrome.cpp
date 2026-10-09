#include "editorial_chrome.hpp"
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

namespace cps::gui {
EditorialChrome::EditorialChrome(QWidget *window, qreal titleHeight, qreal referenceHeight, bool resizable)
        : QObject(window), window_(window), titleHeight_(titleHeight), referenceHeight_(referenceHeight),
          resizable_(resizable) {
        window->setWindowFlag(Qt::FramelessWindowHint);
        window->setWindowIcon(editorialBrandIcon());
        qApp->installEventFilter(this);
#ifdef Q_OS_WIN
        if (QGuiApplication::platformName() == QLatin1String("windows"))
            qApp->installNativeEventFilter(this);
#endif
    }
EditorialChrome::~EditorialChrome() { qApp->removeNativeEventFilter(this); }
void EditorialChrome::updateSystemFrame(bool active) {
#ifdef Q_OS_WIN
        if (!window_ || QGuiApplication::platformName() != QLatin1String("windows") || !window_->internalWinId()) return;
        const auto hwnd = reinterpret_cast<HWND>(window_->internalWinId());
        const DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
        DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
        const int edge = window_->isMaximized() || window_->isFullScreen() ? 0 : 1;
        const MARGINS margins{edge, edge, edge, edge};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        // Windows 11 attributes; older DWM versions simply ignore these hints.
        const DWORD corners = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33, &corners, sizeof(corners));
        const COLORREF border = active ? RGB(206,210,216) : RGB(224,227,231);
        DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));
#else
        Q_UNUSED(active);
#endif
    }
bool EditorialChrome::nativeEventFilter(const QByteArray &, void *message, qintptr *result) {
#ifdef Q_OS_WIN
        if (!window_ || QGuiApplication::platformName() != QLatin1String("windows")) return false;
        const auto *msg = static_cast<MSG *>(message);
        if (msg->hwnd != reinterpret_cast<HWND>(window_->internalWinId())) return false;
        if (msg->message == WM_DWMCOMPOSITIONCHANGED) updateSystemFrame(window_->isActiveWindow());
        if (msg->message == WM_NCCALCSIZE && msg->wParam) { *result = 0; return true; }
        if (msg->message == WM_GETMINMAXINFO && resizable_) {
            auto *info = reinterpret_cast<MINMAXINFO *>(msg->lParam);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfoW(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
                info->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left,
                                       monitor.rcWork.top - monitor.rcMonitor.top};
                info->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left,
                                   monitor.rcWork.bottom - monitor.rcWork.top};
                const qreal dpr = window_->devicePixelRatioF();
                info->ptMinTrackSize = {LONG(window_->minimumWidth() * dpr), LONG(window_->minimumHeight() * dpr)};
                *result = 0;
                return true;
            }
        }
        if (msg->message == WM_NCHITTEST) {
            POINT raw{short(LOWORD(msg->lParam)), short(HIWORD(msg->lParam))};
            ScreenToClient(msg->hwnd, &raw);
            const qreal dpr = window_->devicePixelRatioF();
            const QPoint point(qRound(raw.x / dpr), qRound(raw.y / dpr));
            if (resizable_ && !window_->isMaximized() && !window_->isFullScreen()) {
                const bool left = point.x() < 6, right = point.x() >= window_->width() - 6;
                const bool top = point.y() < 6, bottom = point.y() >= window_->height() - 6;
                const int edge = top && left ? HTTOPLEFT : top && right ? HTTOPRIGHT
                                 : bottom && left ? HTBOTTOMLEFT : bottom && right ? HTBOTTOMRIGHT
                                 : left ? HTLEFT : right ? HTRIGHT : top ? HTTOP : bottom ? HTBOTTOM : 0;
                if (edge) { *result = edge; return true; }
            }
            if (draggable(point)) { *result = HTCAPTION; return true; }
        }
#else
        Q_UNUSED(message);
        Q_UNUSED(result);
#endif
        return false;
    }
bool EditorialChrome::eventFilter(QObject *object, QEvent *event) {
        if (object == window_ && event->type() == QEvent::Show) {
#ifdef Q_OS_WIN
            if (QGuiApplication::platformName() == QLatin1String("windows")) {
                const auto hwnd = reinterpret_cast<HWND>(window_->winId());
                auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
                style |= WS_CAPTION | WS_SYSMENU;
                if (resizable_) style |= WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
                SetWindowLongPtrW(hwnd, GWL_STYLE, style);
                SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                updateSystemFrame(window_->isActiveWindow());
            }
#endif
        }
        if (object == window_ && (event->type() == QEvent::WindowActivate || event->type() == QEvent::WindowDeactivate || event->type() == QEvent::WindowStateChange))
            updateSystemFrame(event->type() == QEvent::WindowActivate || (event->type() != QEvent::WindowDeactivate && window_->isActiveWindow()));
        if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonDblClick)
            return false;
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget || widget->window() != window_) return false;
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) return false;
        const QPoint point = window_->mapFromGlobal(mouse->globalPosition().toPoint());
        if (!draggable(point)) return false;
        if (event->type() == QEvent::MouseButtonDblClick && resizable_) { toggleMaximized(); return true; }
        if (window_->windowHandle()) return window_->windowHandle()->startSystemMove();
        return false;
    }
} // namespace cps::gui
