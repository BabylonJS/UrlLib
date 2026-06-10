// Tests for UrlRequest's transport-level error reporting (ErrorString / ErrorSymbol /
// ErrorCode). All scenarios are offline-deterministic: a local file that exists, a local
// file that doesn't, a loopback port with no listener, and a hostname under the reserved
// `.invalid` TLD (RFC 6761 guarantees it never resolves).

#include <UrlLib/UrlLib.h>

#include <arcana/threading/cancellation.h>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <regex>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
    // Block until SendAsync completes; false on timeout. On timeout the request is
    // aborted and the shared promise keeps the continuation's target alive regardless.
    // (UrlRequest::Abort() currently only interrupts the Windows backend's transport, so
    // the timeout is a CI backstop; the scenarios in this suite are offline-deterministic
    // and complete quickly by construction.)
    [[nodiscard]] bool SendAndWait(UrlLib::UrlRequest& request)
    {
        auto done = std::make_shared<std::promise<void>>();
        auto future = done->get_future();

        request.SendAsync().then(arcana::inline_scheduler, arcana::cancellation::none(),
            [done](const arcana::expected<void, std::exception_ptr>&) {
                done->set_value();
            });

        if (future.wait_for(std::chrono::seconds{30}) != std::future_status::ready)
        {
            request.Abort();
            return false;
        }

        return true;
    }

    // A loopback TCP port that deterministically refuses connections. On Linux the socket
    // is kept bound (reserving the port against reuse by any other process) but never
    // listen()ed on; SYNs to a bound-but-not-listening port are refused with RST, so the
    // setup is race-free. On Darwin that same SYN is silently dropped instead (the connect
    // attempt times out rather than being refused -- verified empirically), so there the
    // socket is closed after reserving the port number and connects get an immediate RST
    // from the now-unbound port; the reuse window between close and connect is
    // microseconds wide, which is acceptable for CI.
    class RefusingPort
    {
    public:
        static std::optional<RefusingPort> Acquire()
        {
            int fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0)
            {
                return std::nullopt;
            }

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            socklen_t addressLength = sizeof(address);
            if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
                ::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &addressLength) != 0)
            {
                ::close(fd);
                return std::nullopt;
            }

#if defined(__APPLE__)
            ::close(fd);
            fd = -1;
#endif

            return RefusingPort{fd, ntohs(address.sin_port)};
        }

        RefusingPort(RefusingPort&& other) noexcept
            : m_fd{other.m_fd}
            , m_port{other.m_port}
        {
            other.m_fd = -1;
        }

        RefusingPort(const RefusingPort&) = delete;
        RefusingPort& operator=(const RefusingPort&) = delete;
        RefusingPort& operator=(RefusingPort&&) = delete;

        ~RefusingPort()
        {
            if (m_fd >= 0)
            {
                ::close(m_fd);
            }
        }

        std::string Url() const
        {
            return "http://127.0.0.1:" + std::to_string(m_port) + "/";
        }

    private:
        RefusingPort(int fd, uint16_t port)
            : m_fd{fd}
            , m_port{port}
        {
        }

        int m_fd;
        uint16_t m_port;
    };

    // RAII temp file with a per-process-unique name, so parallel test runs sharing a temp
    // directory don't collide and no artifacts outlive the test. Pass nullptr contents
    // for a path that is guaranteed not to exist.
    class TempFile
    {
    public:
        TempFile(const char* tag, const char* contents)
            : m_path{std::filesystem::temp_directory_path() /
                  ("urllib_error_reporting_" + std::string{tag} + "_" + std::to_string(::getpid()) + ".tmp")}
        {
            std::error_code ignored{};
            std::filesystem::remove(m_path, ignored);
            if (contents != nullptr)
            {
                std::ofstream stream{m_path, std::ios::trunc};
                stream << contents;
            }
        }

        ~TempFile()
        {
            std::error_code ignored{};
            std::filesystem::remove(m_path, ignored);
        }

        std::string Url() const
        {
            return "file://" + m_path.generic_string();
        }

    private:
        std::filesystem::path m_path;
    };
}

TEST(UrlRequestErrorReporting, SuccessfulLocalFileReportsNoError)
{
    const TempFile file{"ok", "hello urllib"};

    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, file.Url());
    request.ResponseType(UrlLib::UrlResponseType::String);
    ASSERT_TRUE(SendAndWait(request));

    EXPECT_EQ(request.StatusCode(), UrlLib::UrlStatusCode::Ok);
    EXPECT_EQ(request.ResponseString(), "hello urllib");
    EXPECT_TRUE(request.ErrorString().empty()) << request.ErrorString();
    EXPECT_TRUE(request.ErrorSymbol().empty()) << request.ErrorSymbol();
    EXPECT_EQ(request.ErrorCode(), 0);
}

TEST(UrlRequestErrorReporting, MissingLocalFileReportsError)
{
    const TempFile missing{"missing", nullptr};

    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, missing.Url());
    ASSERT_TRUE(SendAndWait(request));

    EXPECT_EQ(request.StatusCode(), UrlLib::UrlStatusCode::None);
    EXPECT_FALSE(request.ErrorString().empty());
#if defined(__APPLE__)
    EXPECT_EQ(request.ErrorSymbol(), "NSURLErrorFileDoesNotExist") << request.ErrorString();
    EXPECT_EQ(request.ErrorCode(), -1100) << request.ErrorString();
#else
    EXPECT_EQ(request.ErrorSymbol(), "CURLE_FILE_COULDNT_READ_FILE") << request.ErrorString();
    EXPECT_EQ(request.ErrorCode(), 37) << request.ErrorString();
#endif
}

TEST(UrlRequestErrorReporting, ConnectionRefusedReportsError)
{
    const auto port = RefusingPort::Acquire();
    ASSERT_TRUE(port.has_value());

    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, port->Url());
    ASSERT_TRUE(SendAndWait(request));

    EXPECT_EQ(request.StatusCode(), UrlLib::UrlStatusCode::None);
    EXPECT_FALSE(request.ErrorString().empty());
#if defined(__APPLE__)
    EXPECT_EQ(request.ErrorSymbol(), "NSURLErrorCannotConnectToHost") << request.ErrorString();
    EXPECT_EQ(request.ErrorCode(), -1004) << request.ErrorString();
#else
    EXPECT_EQ(request.ErrorSymbol(), "CURLE_COULDNT_CONNECT") << request.ErrorString();
    EXPECT_EQ(request.ErrorCode(), 7) << request.ErrorString();
    // libcurl's CURLOPT_ERRORBUFFER detail names the host it could not reach.
    EXPECT_NE(request.ErrorString().find("127.0.0.1"), std::string_view::npos) << request.ErrorString();
#endif
}

TEST(UrlRequestErrorReporting, DnsResolutionFailureReportsError)
{
    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, "http://urllib-error-reporting-test.invalid/");
    ASSERT_TRUE(SendAndWait(request));

    EXPECT_EQ(request.StatusCode(), UrlLib::UrlStatusCode::None);
    EXPECT_FALSE(request.ErrorString().empty());
#if defined(__APPLE__)
    // Depending on resolver/network state this surfaces as CannotFindHost (-1003),
    // DNSLookupFailed (-1006), or NotConnectedToInternet (-1009); all are NSURL errors.
    EXPECT_EQ(request.ErrorString().substr(0, 6), "nsurl:") << request.ErrorString();
    EXPECT_NE(request.ErrorCode(), 0) << request.ErrorString();
#else
    EXPECT_EQ(request.ErrorSymbol(), "CURLE_COULDNT_RESOLVE_HOST") << request.ErrorString();
    EXPECT_EQ(request.ErrorCode(), 6) << request.ErrorString();
#endif
}

TEST(UrlRequestErrorReporting, ErrorStringMatchesGreppableGrammar)
{
    const auto port = RefusingPort::Acquire();
    ASSERT_TRUE(port.has_value());

    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, port->Url());
    ASSERT_TRUE(SendAndWait(request));

    // "<domain>:<symbol>(<code>): <detail>" with domain/symbol as stable ASCII tokens.
    const std::regex grammar{R"(^[A-Za-z0-9_.\-]+:[A-Za-z0-9_.\-]+\(-?[0-9]+\): .+)"};
    const std::string errorString{request.ErrorString()};
    EXPECT_TRUE(std::regex_search(errorString, grammar)) << errorString;
}

TEST(UrlRequestErrorReporting, ReopenClearsPriorError)
{
    const auto port = RefusingPort::Acquire();
    ASSERT_TRUE(port.has_value());

    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, port->Url());
    ASSERT_TRUE(SendAndWait(request));
    ASSERT_FALSE(request.ErrorString().empty());

    const TempFile file{"reuse", "reused"};
    request.Open(UrlLib::UrlMethod::Get, file.Url());
    EXPECT_TRUE(request.ErrorString().empty()) << request.ErrorString();
    EXPECT_EQ(request.ErrorCode(), 0);

    request.ResponseType(UrlLib::UrlResponseType::String);
    ASSERT_TRUE(SendAndWait(request));
    EXPECT_EQ(request.StatusCode(), UrlLib::UrlStatusCode::Ok);
    EXPECT_EQ(request.ResponseString(), "reused");
    EXPECT_TRUE(request.ErrorString().empty()) << request.ErrorString();
}

#if defined(__APPLE__)
TEST(UrlRequestErrorReporting, MissingAppResourceReportsError)
{
    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, "app:///urllib_error_reporting_missing.js");
    ASSERT_TRUE(SendAndWait(request));

    EXPECT_EQ(request.StatusCode(), UrlLib::UrlStatusCode::None);
    EXPECT_EQ(request.ErrorSymbol(), "AppResourceNotFound") << request.ErrorString();
    const std::string errorString{request.ErrorString()};
    EXPECT_NE(errorString.find("urllib:AppResourceNotFound(0): "), std::string::npos) << errorString;
    EXPECT_NE(errorString.find("app:///urllib_error_reporting_missing.js"), std::string::npos) << errorString;
}
#endif
