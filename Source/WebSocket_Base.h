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

         ReadyState GetReadyState()
         {
             return m_readyState;
         }
    

     protected:

        std::string m_url;
        ReadyState m_readyState{ReadyState::Closed};
     };
 }
