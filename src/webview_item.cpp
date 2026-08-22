#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include "WebView2.h"

#include "webview_item.h"
#include <QDebug>
#include <atomic>
#include <functional>

class UnknownBase {
protected:
    std::atomic<ULONG> m_ref{1};
    virtual ~UnknownBase() = default;
    ULONG STDMETHODCALLTYPE AddRef()  { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
};


class EnvironmentCompletedHandler : public UnknownBase, public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
public:
    using Fn = std::function<HRESULT(HRESULT, ICoreWebView2Environment *)>;
    explicit EnvironmentCompletedHandler(Fn fn) : m_fn(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Environment *env) override { return m_fn(hr, env); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *>(this);
            AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return UnknownBase::AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return UnknownBase::Release(); }
private:
    Fn m_fn;
};


class ControllerCompletedHandler : public UnknownBase, public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:
    using Fn = std::function<HRESULT(HRESULT, ICoreWebView2Controller *)>;
    explicit ControllerCompletedHandler(Fn fn) : m_fn(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT hr, ICoreWebView2Controller *ctrl) override { return m_fn(hr, ctrl); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *>(this);
            AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return UnknownBase::AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return UnknownBase::Release(); }
private:
    Fn m_fn;
};


class WebMessageReceivedHandler : public UnknownBase, public ICoreWebView2WebMessageReceivedEventHandler {
public:
    using Fn = std::function<HRESULT(ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *)>;
    explicit WebMessageReceivedHandler(Fn fn) : m_fn(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, ICoreWebView2WebMessageReceivedEventArgs *args) override { return m_fn(sender, args); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
            *ppv = static_cast<ICoreWebView2WebMessageReceivedEventHandler *>(this);
            AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef()  override { return UnknownBase::AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return UnknownBase::Release(); }
private:
    Fn m_fn;
};

// Child window
namespace {
const wchar_t *kChildClassName = L"RakshaWebViewChildWindow";
bool           g_classRegistered = false;
}

long long __stdcall WebViewItem::childWndProc(HWND_OPAQUE hwnd, unsigned int msg, unsigned long long wParam, long long lParam)
{
    HWND hw = reinterpret_cast<HWND>(hwnd);
    if (msg == WM_ERASEBKGND) return 1;
    return DefWindowProcW(hw, msg, wParam, lParam);
}

WebViewItem::WebViewItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, false);
    setAcceptedMouseButtons(Qt::NoButton);
}

WebViewItem::~WebViewItem() { teardown(); }

void WebViewItem::setUrl(const QString &url)
{
    if (m_url == url) return;
    m_url = url;
    emit urlChanged();
    if (m_webview.p)
        navigateTo(url);
    else
        m_pendingNavigate = true;
}

void WebViewItem::setInputEnabled(bool enabled)
{
    if (m_inputEnabled == enabled) return;
    m_inputEnabled = enabled;
    emit inputEnabledChanged();
    if (m_childHwnd) {

        EnableWindow(reinterpret_cast<HWND>(m_childHwnd), enabled ? TRUE : FALSE);
    }
}

void WebViewItem::navigateTo(const QString &url)
{
    if (!m_webview.p || url.isEmpty()) return;
    qDebug() << "WebViewItem: navigating to" << url;
    m_webview->Navigate(reinterpret_cast<LPCWSTR>(url.utf16()));
}

void WebViewItem::itemChange(ItemChange change, const ItemChangeData &data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange) {
        if (data.window && !m_childHwnd) {
            createNativeChildWindow();
            initWebView2();
        } else if (!data.window) {
            teardown();
        }
    } else if (change == ItemVisibleHasChanged) {
        updateVisibility();
    }
}

void WebViewItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    updateBounds();
}

void WebViewItem::createNativeChildWindow()
{
    HINSTANCE hInstance = GetModuleHandleW(nullptr);
    if (!g_classRegistered) {
        WNDCLASSW wc     = {};
        wc.lpfnWndProc   = reinterpret_cast<WNDPROC>(&WebViewItem::childWndProc);
        wc.hInstance     = hInstance;
        wc.lpszClassName = kChildClassName;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        g_classRegistered = true;
    }

    HWND parentHwnd = reinterpret_cast<HWND>(window()->winId());
    const QPointF pos = mapToScene(QPointF(0, 0));
    const qreal dpr = window()->devicePixelRatio();

    HWND hw = CreateWindowExW(
        0, kChildClassName, L"",
        WS_CHILD | WS_VISIBLE,
        static_cast<int>(pos.x()  * dpr),
        static_cast<int>(pos.y()  * dpr),
        static_cast<int>(width()  * dpr),
        static_cast<int>(height() * dpr),
        parentHwnd, nullptr, hInstance, nullptr);

    m_childHwnd = reinterpret_cast<HWND_OPAQUE>(hw);
    if (!hw)
        qWarning() << "WebViewItem: CreateWindowExW failed, error" << GetLastError();
}

void WebViewItem::initWebView2()
{
    if (!m_childHwnd) return;
    HWND hw = reinterpret_cast<HWND>(m_childHwnd);

    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", L"--allow-file-access-from-files --disable-web-security");

    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        new EnvironmentCompletedHandler(
            [this, hw](HRESULT res, ICoreWebView2Environment *env) -> HRESULT {
                if (FAILED(res) || !env) {
                    emit loadFailed(QStringLiteral("Environment failed: 0x%1").arg(static_cast<uint>(res), 8, 16, QChar('0')));
                    return S_OK;
                }
                env->AddRef();
                m_environment.Attach(env);

                env->CreateCoreWebView2Controller(
                    hw,
                    new ControllerCompletedHandler(
                        [this](HRESULT res2, ICoreWebView2Controller *ctrl) -> HRESULT {
                            if (FAILED(res2) || !ctrl) {
                                emit loadFailed(QStringLiteral("Controller failed: 0x%1").arg(static_cast<uint>(res2), 8, 16, QChar('0')));
                                return S_OK;
                            }
                            ctrl->AddRef();
                            m_controller.Attach(ctrl);

                            ICoreWebView2 *wv = nullptr;
                            m_controller->get_CoreWebView2(&wv);
                            m_webview.Attach(wv);

                            // Relay JS postMessage -> Qt signal
                            m_webview->add_WebMessageReceived(
                                new WebMessageReceivedHandler(
                                    [this](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
                                        LPWSTR raw = nullptr;
                                        args->TryGetWebMessageAsString(&raw);
                                        if (raw) {
                                            emit webMessageReceived(QString::fromWCharArray(raw));
                                            CoTaskMemFree(raw);
                                        }
                                        return S_OK;
                                    }),
                                nullptr);

                            updateBounds();
                            updateVisibility();
                            emit readyChanged();
                            qDebug() << "WebView2 ready: true";

                            if (!m_url.isEmpty()) {
                                m_pendingNavigate = false;
                                navigateTo(m_url);
                            }
                            return S_OK;
                        }));
                return S_OK;
            }));

    if (FAILED(hr))
        qWarning() << "WebViewItem: init failed, HRESULT" << Qt::hex << hr;
}

void WebViewItem::updateBounds()
{
    if (!m_childHwnd) return;
    HWND hw = reinterpret_cast<HWND>(m_childHwnd);
    const qreal dpr = window() ? window()->devicePixelRatio() : 1.0;

    if (window()) {
        const QPointF pos = mapToScene(QPointF(0, 0));
        MoveWindow(hw,
                   static_cast<int>(pos.x()  * dpr),
                   static_cast<int>(pos.y()  * dpr),
                   static_cast<int>(width()  * dpr),
                   static_cast<int>(height() * dpr),
                   TRUE);
    }
    if (m_controller.p) {
        RECT bounds = {0, 0, static_cast<LONG>(width()  * dpr), static_cast<LONG>(height() * dpr)};
        m_controller->put_Bounds(bounds);
    }
}

void WebViewItem::updateVisibility()
{
    HWND hw = reinterpret_cast<HWND>(m_childHwnd);
    const bool show = isVisible() && window() != nullptr;
    if (m_controller.p)
        m_controller->put_IsVisible(show ? TRUE : FALSE);
    if (hw)
        ShowWindow(hw, show ? SW_SHOW : SW_HIDE);
}

void WebViewItem::openDevTools()
{
    if (!m_webview.p) return;
    m_webview->OpenDevToolsWindow();
}

void WebViewItem::executeScript(const QString &js)
{
    if (!m_webview.p || js.isEmpty()) return;
    m_webview->ExecuteScript(reinterpret_cast<LPCWSTR>(js.utf16()), nullptr);
}

void WebViewItem::navigateToHtml(const QString &html)
{
    if (!m_webview.p || html.isEmpty()) return;
    m_webview->NavigateToString(reinterpret_cast<LPCWSTR>(html.utf16()));
}

void WebViewItem::teardown()
{
    if (m_controller.p) {
        m_controller->Close();
        m_controller.Reset();
    }
    m_webview.Reset();
    m_environment.Reset();

    if (m_childHwnd) {
        DestroyWindow(reinterpret_cast<HWND>(m_childHwnd));
        m_childHwnd = nullptr;
    }
}