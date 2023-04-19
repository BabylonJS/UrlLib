namespace UrlLib
{
    WebSocket::WebSocket()
    : m_impl{std::make_unique<WebSocket::Impl>()}
    {
    }

    WebSocket::~WebSocket() = default;

    // Copy semantics
    WebSocket::WebSocket(const WebSocket&) = default;
    WebSocket& WebSocket::operator=(const WebSocket&) = default;

    // Move semantics
    WebSocket::WebSocket(WebSocket&&) noexcept = default;
    WebSocket& WebSocket::operator=(WebSocket&&) noexcept = default;

    void WebSocket::Close()
    {
        m_impl->Close();
    }

    void WebSocket::Send(std::string message)
    {
        m_impl->Send(message);
    }

    void WebSocket::Open(std::string url,
        std::function<void()> onopen,
        std::function<void()> onclose,
        std::function<void(std::string)> onmessage,
        std::function<void()> onerror)
    {
        m_impl->Open(url, onopen, onclose, onmessage, onerror);
    }
}
