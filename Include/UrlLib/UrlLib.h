#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <arcana/threading/task.h>
#include <unordered_map>

namespace UrlLib
{
    enum class UrlStatusCode
    {
        None = 0,
        Ok = 200,
    };

    enum class UrlMethod
    {
        Get,
        Post
    };

    enum class UrlResponseType
    {
        String,
        Buffer,
    };

    class UrlRequest final
    {
    public:
        UrlRequest();
        ~UrlRequest();

        // Copy semantics
        UrlRequest(const UrlRequest&);
        UrlRequest& operator=(const UrlRequest&);

        // Move semantics
        UrlRequest(UrlRequest&&) noexcept;
        UrlRequest& operator=(UrlRequest&&) noexcept;

        void Abort();

        void Open(UrlMethod method, const std::string& url);

        UrlResponseType ResponseType() const;

        void ResponseType(UrlResponseType value);

        arcana::task<void, std::exception_ptr> SendAsync();

        void SetRequestBody(std::string requestBody);

        void SetRequestHeader(std::string name, std::string value);

        std::optional<std::string> GetResponseHeader(const std::string& headerName) const;

        const std::unordered_map<std::string, std::string>& GetAllResponseHeaders() const;
        
        UrlStatusCode StatusCode() const;

        std::string_view StatusText() const;

        // Transport-level error reporting. All three return empty/zero when the request did
        // not fail at the transport layer (note that an HTTP error status like 404 is NOT a
        // transport failure). ErrorString() is normalized for log-pipeline filtering:
        //   "<domain>:<symbol>(<code>): <detail>"
        // where <domain> and <symbol> are stable ASCII tokens (e.g. "curl",
        // "CURLE_COULDNT_CONNECT") and <detail> is the platform's human-readable message.
        std::string_view ErrorString() const;

        // The stable, platform-specific symbolic name of the failure, e.g.
        // "CURLE_COULDNT_RESOLVE_HOST" or "NSURLErrorTimedOut".
        std::string_view ErrorSymbol() const;

        // The raw numeric platform error code (CURLcode, NSError code, etc.).
        int32_t ErrorCode() const;

        std::string_view ResponseUrl() const;

        std::string_view ResponseString() const;

        gsl::span<const std::byte> ResponseBuffer() const;

    private:
        class Impl;
        class ImplBase;

        std::shared_ptr<Impl> m_impl{};
    };

    class WebSocket final
    {
    public:
        WebSocket(const std::string& url, std::function<void()> onOpen, std::function<void(int, const std::string&)> onClose, std::function<void(const std::string&)> onMessage, std::function<void(const std::string&)> onError);
        ~WebSocket();

        // Copy semantics
        WebSocket(const WebSocket&);
        WebSocket& operator=(const WebSocket&);

        // Move semantics
        WebSocket(WebSocket&&) noexcept;
        WebSocket& operator=(WebSocket&&) noexcept;

        std::string GetURL();

        void Open();
        void Close();
        void Send(std::string message);

    private:
        class Impl;
        class ImplBase;

        std::shared_ptr<Impl> m_impl{};
    };
}
