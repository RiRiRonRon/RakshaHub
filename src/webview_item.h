#pragma once
#include <QQuickItem>
#include <QQuickWindow>
#include <QUrl>



struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;


template<typename T>
class SimpleComPtr {
public:
    SimpleComPtr() : p(nullptr) {}
    ~SimpleComPtr() { Reset(); }
    SimpleComPtr(const SimpleComPtr &) = delete;
    SimpleComPtr &operator=(const SimpleComPtr &) = delete;
    T *Get() const { return p; }
    T *operator->() const { return p; }
    operator bool() const { return p != nullptr; }
    T **operator&() { return &p; }
    void Reset() {
        if (p) { p->Release(); p = nullptr; }
    }
    void Attach(T *ptr) {
        Reset();
        p = ptr;
    }
    T *p = nullptr;
};


typedef void *HWND_OPAQUE;

class WebViewItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString url  READ url    WRITE setUrl  NOTIFY urlChanged)
    Q_PROPERTY(bool    ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(bool inputEnabled READ inputEnabled WRITE setInputEnabled NOTIFY inputEnabledChanged)

public:
    explicit WebViewItem(QQuickItem *parent = nullptr);
    ~WebViewItem() override;

    QString url()     const { return m_url; }
    void    setUrl(const QString &url);
    bool    isReady() const { return m_controller.p != nullptr; }

    bool    inputEnabled() const { return m_inputEnabled; }
    void    setInputEnabled(bool enabled);

    Q_INVOKABLE void openDevTools();
    Q_INVOKABLE void executeScript(const QString &js);
    Q_INVOKABLE void navigateToHtml(const QString &html);

signals:
    void urlChanged();
    void readyChanged();
    void loadFailed(const QString &reason);
    void webMessageReceived(const QString &json);
    void inputEnabledChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void createNativeChildWindow();
    void initWebView2();
    void updateBounds();
    void updateVisibility();
    void teardown();
    void navigateTo(const QString &url);

    static long long __stdcall childWndProc(HWND_OPAQUE hwnd, unsigned int msg, unsigned long long wParam, long long lParam);

    QString      m_url;
    HWND_OPAQUE  m_childHwnd    = nullptr;
    bool         m_pendingNavigate = false;
    bool         m_inputEnabled = true;

    SimpleComPtr<ICoreWebView2Environment> m_environment;
    SimpleComPtr<ICoreWebView2Controller>  m_controller;
    SimpleComPtr<ICoreWebView2>            m_webview;
};