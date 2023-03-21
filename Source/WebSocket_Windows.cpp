#include "WebSocket_Base.h"

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Networking.Sockets.h>

#include <arcana/threading/task_conversions.h>

#include <iostream>

namespace UrlLib
{
    using namespace winrt;

    class WebSocket::WSImpl : public WSImplBase
    {
    public:
        
        void Send(std::string message)
        {
            if (m_readyState == ReadyState::Closed ||
                m_readyState == ReadyState::Closing ||
                m_readyState == ReadyState::Connecting)
            {
                error_callback();

                throw std::runtime_error{"WebSocket is not Open"};
            }

            try
            {
                // SEND MESSAGE
                Windows::Storage::Streams::DataWriter dataWriter{m_webSocket.OutputStream()};
                dataWriter.WriteString(winrt::to_hstring(message));

                arcana::create_task<std::exception_ptr>(dataWriter.StoreAsync())
                    .then(arcana::inline_scheduler, arcana::cancellation::none(), [this, dataWriter](int)
                        {
                            dataWriter.DetachStream();
                        });
            }
            catch (winrt::hresult_error const& )
            {
                error_callback();
            }
        }

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

            m_url = url;

            m_readyState = ReadyState::Connecting;

            m_webSocket.Control().MessageType(Windows::Networking::Sockets::SocketMessageType::Utf8);

            m_messageReceivedEventToken = m_webSocket.MessageReceived({this, &WebSocket::WSImpl::OnMessageReceived});
            m_closedEventToken = m_webSocket.Closed({this, &WebSocket::WSImpl::OnWebSocketClosed});

            try
            {
                // INITIALIZE SOCKET
                hstring hURL = to_hstring(m_url);

                arcana::create_task<std::exception_ptr>(m_webSocket.ConnectAsync(Windows::Foundation::Uri{hURL}))
                    .then(arcana::inline_scheduler, arcana::cancellation::none(), [this]()
                        {
                            m_readyState = ReadyState::Open;
                            
                            open_callback();
                        });
            }
            catch (hresult_error const& )
            {
                error_callback();
            }
        }

        void Close()
        {
            if (m_readyState == ReadyState::Closing ||
                m_readyState == ReadyState::Closed)
            {
                error_callback();

                throw std::runtime_error{"WebSocket is already Closing/Closed"};
            }

            m_readyState = ReadyState::Closing;

            m_webSocket.Close();

            m_readyState = ReadyState::Closed;

            close_callback();
        }

    private:
        void OnWebSocketClosed(Windows::Networking::Sockets::IWebSocket const& /* sender */, Windows::Networking::Sockets::WebSocketClosedEventArgs const& args)
        {
            std::cout << L"WebSocket_Closed; Code: " << args.Code() << ", Reason: \"" << args.Reason().c_str() << "\"" << std::endl;
        }

        void OnMessageReceived(Windows::Networking::Sockets::MessageWebSocket const& /* sender */, Windows::Networking::Sockets::MessageWebSocketMessageReceivedEventArgs const& args)
        {
            if (m_readyState == ReadyState::Closed ||
                m_readyState == ReadyState::Closing)
            {
                error_callback();

                throw std::runtime_error{"WebSocket is Closing/Closed"};
            }

            try
            {
                Windows::Storage::Streams::DataReader dataReader{args.GetDataReader()};

                dataReader.UnicodeEncoding(Windows::Storage::Streams::UnicodeEncoding::Utf8);
                std::string message = winrt::to_string(dataReader.ReadString(dataReader.UnconsumedBufferLength()));

                message_callback(message);
            }
            catch (winrt::hresult_error const& )
            {
                error_callback();
            }
        }
       
        Windows::Networking::Sockets::MessageWebSocket m_webSocket;

        std::function<void(void)> open_callback;
        std::function<void(void)> close_callback;
        std::function<void(std::string)> message_callback;
        std::function<void(void)> error_callback;

        event_token m_messageReceivedEventToken;
        event_token m_closedEventToken;
    };
}

#include "WebSocket_Shared.h"
