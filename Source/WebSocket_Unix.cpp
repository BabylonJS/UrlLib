#include "WebSocket_Base.h"

#include <curl/curl.h>
#include <unistd.h>
#include <filesystem>
#include <cassert>
#include <sstream>

namespace UrlLib
{
    class WebSocket::Impl : public ImplBase
    {
    public:
        Impl(const std::string& url, std::function<void()> onOpen, std::function<void(int, const std::string&)> onClose, std::function<void(const std::string&)> onMessage, std::function<void(const std::string&)> onError)
        : ImplBase{url, onOpen, onClose, onMessage, onError}
        {
        }

        ~Impl()
        {
        }
        
        void Send(std::string /*message*/)
        {
        }

        void Open()
        {
        }

        void Close()
        {
        }

    private:

    };
}

#include "WebSocket_Shared.h"
