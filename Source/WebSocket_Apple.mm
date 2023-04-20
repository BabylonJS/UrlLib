#include "WebSocket_Base.h"
#include "WebSocket_Apple_ObjC.h"

namespace UrlLib
{
class API_AVAILABLE(ios(13.0)) WebSocket::Impl : public ImplBase
    {
    public:
        void Open(std::string url,
            std::function<void()> onopen,
            std::function<void()> onclose,
            std::function<void(std::string)> onmessage,
            std::function<void()> onerror)
        {
            // handle callbacks
            void (^openCallback)() =  [onopen]()
            {
                onopen();
            };
            void (^closeCallback)() =  [onclose]()
            {
                onclose();
            };
            void (^messageCallback)(NSString* messageStr) =  [onmessage](NSString* messageStr)
            {
                std::string cppString( [messageStr UTF8String] );
                onmessage(cppString);
            };
            void (^errorCallback)() =  [onerror]()
            {
                onerror();
            };
            
            webSocket = [[WebSocket_ObjC alloc] init];
            NSString *messageString = @(url.data());
            [webSocket open:messageString on_open:openCallback on_close:closeCallback on_message:messageCallback on_error:errorCallback];
        }
        
        void Send(std::string message)
        {
            [webSocket sendMessage:@(message.data())];
        }

        void Close()
        {
            [webSocket close];
        }
 
    private:
        WebSocket_ObjC *webSocket;
    };
}

#include "WebSocket_Shared.h"
