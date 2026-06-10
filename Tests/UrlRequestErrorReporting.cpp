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
    // Block until SendAsync completes. Returns false on timeout (a hung transport), in
    // which case the shared promise keeps the continuation's target alive regardless.
    [[nodiscard]] bool SendAndWait(UrlLib::UrlRequest& request)
    {
        auto done = std::make_shared<std::promise<void>>();
        auto future = done->get_future();

        request.SendAsync().then(arcana::inline_scheduler, arcana::cancellation::none(),
            [done](const arcana::expected<void, std::exception_ptr>&) {
                done->set_value();
            });

        return future.wait_for(std::chrono::seconds{120}) == std::future_status::ready;
    }

    // Bind an ephemeral loopback port, then close it: connecting to it afterwards is
    // refused (nothing is listening, and the OS won't immediately reassign it).
    uint16_t AcquireClosedPort()
    {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_GE(fd, 0);

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        EXPECT_EQ(::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);

        socklen_t addressLength = sizeof(address);
        EXPECT_EQ(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &addressLength), 0);
        const uint16_t port = ntohs(address.sin_port);

        ::close(fd);
        return port;
    }

    std::filesystem::path WriteTempFile(const char* name, const char* contents)
    {
        const auto path = std::filesystem::temp_directory_path() / name;
        std::ofstream stream{path, std::ios::trunc};
        stream << contents;
        return path;
    }
}

TEST(UrlRequestErrorReporting, SuccessfulLocalFileReportsNoError)
{
    const auto path = WriteTempFile("urllib_error_reporting_ok.txt", "hello urllib");

    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, "file://" + path.generic_string());
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
    const auto path = std::filesystem::temp_directory_path() / "urllib_error_reporting_definitely_missing.bin";
    std::filesystem::remove(path);

    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, "file://" + path.generic_string());
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
    const uint16_t port = AcquireClosedPort();

    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, "http://127.0.0.1:" + std::to_string(port) + "/");
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
    const uint16_t port = AcquireClosedPort();

    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, "http://127.0.0.1:" + std::to_string(port) + "/");
    ASSERT_TRUE(SendAndWait(request));

    // "<domain>:<symbol>(<code>): <detail>" with domain/symbol as stable ASCII tokens.
    const std::regex grammar{R"(^[A-Za-z0-9_.\-]+:[A-Za-z0-9_.\-]+\(-?[0-9]+\): .+)"};
    const std::string errorString{request.ErrorString()};
    EXPECT_TRUE(std::regex_search(errorString, grammar)) << errorString;
}

TEST(UrlRequestErrorReporting, ReopenClearsPriorError)
{
    const uint16_t port = AcquireClosedPort();

    UrlLib::UrlRequest request{};
    request.Open(UrlLib::UrlMethod::Get, "http://127.0.0.1:" + std::to_string(port) + "/");
    ASSERT_TRUE(SendAndWait(request));
    ASSERT_FALSE(request.ErrorString().empty());

    const auto path = WriteTempFile("urllib_error_reporting_reuse.txt", "reused");
    request.Open(UrlLib::UrlMethod::Get, "file://" + path.generic_string());
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
