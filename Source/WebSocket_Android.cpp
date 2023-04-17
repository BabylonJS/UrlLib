#include "WebSocket_Base.h"
#include <AndroidExtensions/Globals.h>
#include <AndroidExtensions/JavaWrappers.h>

using namespace android::global;
using namespace android::net;
using namespace java::lang;
using namespace java::io;
using namespace java::net;
using namespace java::websocket;

namespace UrlLib
{
    class WebSocket::WSImpl : public WSImplBase
    {
    public:
        ~WSImpl()
        {
            delete m_webSocket;
        }

        void Open(std::string url,
            std::function<void()> onopen,
            std::function<void()> onclose,
            std::function<void(std::string)> onmessage,
            std::function<void()> onerror)
        {
            open_callback = onopen;
            close_callback = onclose;
            message_callback = onmessage;
            error_callback = onerror;

            m_url = url;
            m_webSocket = new WebSocketClient(url, open_callback, close_callback, message_callback, error_callback);
        }

        void Send(std::string message)
        {
            if (!m_webSocket)
            {
                throw std::runtime_error{"WebSocket is not initialized"};
            }

            m_webSocket->Send(message);
        }

        void Close()
        {
            m_webSocket->Close();
        }

    private:     
        WebSocketClient* m_webSocket;
        std::function<void()> open_callback;
        std::function<void()> close_callback;
        std::function<void(std::string)> message_callback;
        std::function<void()> error_callback;
    };
}

#include "WebSocket_Shared.h"
