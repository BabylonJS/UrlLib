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
        void Open(std::string url,
                     std::function<void(void)> onopen,
                     std::function<void(void)> onclose,
                     std::function<void(std::string)> onmessage,
                     std::function<void(void)> onerror)
        {
            open_callback = onopen;
            close_callback = onclose;
            message_callback = onmessage;
            error_callback = onerror;

            m_readyState = ReadyState::Connecting;
            m_webSocket = new WebSocketClient(url);
        }

        void Send(std::string message)
        {
            if (!m_webSocket)
            {
                throw std::runtime_error{"WebSocket is not initialized"};
            }

            if (m_readyState == ReadyState::Closed ||
                m_readyState == ReadyState::Closing ||
                m_readyState == ReadyState::Connecting)
            {
                //error_callback();
                throw std::runtime_error{"WebSocket is not open"};
            }

            m_webSocket->Send(message);
        }

        void Close()
        {
            if (m_readyState == ReadyState::Closing ||
                m_readyState == ReadyState::Closed)
            {
                //error_callback();
                throw std::runtime_error{"WebSocket is already Closing/Closed"};
            }

            m_webSocket->Close();
            //close_callback();
        }

    private:     
        WebSocketClient* m_webSocket;
        std::function<void(void)> open_callback;
        std::function<void(void)> close_callback;
        std::function<void(std::string)> message_callback;
        std::function<void(void)> error_callback;
    };
}

#include "WebSocket_Shared.h"
