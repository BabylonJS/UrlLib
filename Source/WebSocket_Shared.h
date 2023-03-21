namespace UrlLib
{
    WebSocket::WebSocket()
    : m_impl_ws{std::make_unique<WebSocket::WSImpl>()}
    {
    }
    
    WebSocket::~WebSocket() = default;
    
    // Copy semantics
    WebSocket::WebSocket(const WebSocket&) = default;
    WebSocket& WebSocket::operator=(const WebSocket&) = default;
    
    // Move semantics
    WebSocket::WebSocket(WebSocket&&) noexcept = default;
    WebSocket& WebSocket::operator=(WebSocket&&) noexcept = default;
    
    ReadyState WebSocket::GetReadyState()
    {
        return m_impl_ws->GetReadyState();
    }
    
    std::string WebSocket::GetURL()
    {
        return m_impl_ws->GetURL();
    }
    
    void WebSocket::Close()
    {
        m_impl_ws->Close();
    }
    
    void WebSocket::Send(std::string message)
    {
        m_impl_ws->Send(message);
    }
    
    void WebSocket::Open(std::string url,
                         std::function<void(void)> onopen,
                         std::function<void(void)> onclose,
                         std::function<void(std::string)> onmessage,
                         std::function<void(void)> onerror)
    {
        m_impl_ws->Open(url, onopen, onclose, onmessage, onerror);
    }
}
