#include "WebSocket_Base.h"
#include "WebSocketImpl_Apple.h"

namespace UrlLib
{
class API_AVAILABLE(ios(13.0)) WebSocket::WSImpl : public WSImplBase
    {
    public:
        void Close()
        {
            [webSocket close];
        }
        
        void Open(std::string url,
            std::function<void()> onopen,
            std::function<void()> onclose,
            std::function<void(std::string)> onmessage,
            std::function<void()> onerror)
        {
            m_onOpen = onopen;
            m_onClose = onclose;
            m_onMessage = onmessage;
            m_onError = onerror;
            
            // handle callbacks
            void (^openCallback)() =  [this]()
            {
                m_onOpen();
            };
            void (^closeCallback)() =  [this]()
            {
                m_onClose();
            };
            void (^messageCallback)(NSString* messageStr) =  [this](NSString* messageStr)
            {
                std::string cppString( [messageStr UTF8String] );
                m_onMessage(cppString);
            };
            void (^errorCallback)() =  [this]()
            {
                m_onError();
            };
            
            webSocket = [[WebSocket_Impl alloc] init];
            NSString *messageString = @(url.data());
            [webSocket open:messageString on_open:openCallback on_close:closeCallback on_message:messageCallback on_error:errorCallback];
        }
        
        void Send(std::string message)
        {
            [webSocket sendMessage:@(message.data())];
        }
 
    private:
        WebSocket_Impl *webSocket;
        std::function<void()> m_onOpen;
        std::function<void()> m_onClose;
        std::function<void(std::string)> m_onMessage;
        std::function<void()> m_onError;
    };
}

#include "WebSocket_Shared.h"
