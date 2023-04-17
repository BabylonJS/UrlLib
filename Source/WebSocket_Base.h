#pragma once

#include "UrlLib/UrlLib.h"

namespace UrlLib
{
    class WebSocket::WSImplBase
    {
    public:
        ~WSImplBase()
        {
        }

        std::string GetURL()
        {
            return m_url;
        }
    
    protected:
        std::string m_url;
    };
}
