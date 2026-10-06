#include "UrlRequest_Base.h"

#include <arcana/threading/task_schedulers.h>
#include <android/asset_manager.h>
#include <AndroidExtensions/Globals.h>
#include <AndroidExtensions/JavaWrappers.h>
#include <algorithm>

using namespace android::global;
using namespace android::net;
using namespace java::lang;
using namespace java::io;
using namespace java::net;

namespace UrlLib
{
    namespace
    {
        void ThrowIfFaulted(JNIEnv* env)
        {
            if (auto error = env->ExceptionOccurred())
            {
                env->ExceptionClear();
                auto release = gsl::finally([&] { env->DeleteLocalRef(error); });
                throw Throwable{error};
            }
        }

        jobject GetResponseStream(URLConnection& connection, int statusCode)
        {
            auto env = GetEnvForCurrentThread();
            // HttpURLConnection throws for HTTP error responses via getInputStream;
            // their bodies are still successful transfers, exposed by getErrorStream.
            auto method = env->GetMethodID(connection.GetClass(),
                statusCode >= 400 ? "getErrorStream" : "getInputStream", "()Ljava/io/InputStream;");
            ThrowIfFaulted(env);
            auto stream = env->CallObjectMethod(connection, method);
            ThrowIfFaulted(env);
            return stream;
        }

        template<typename T> void LoadAsset(AAssetManager* assetManager, const char* url, T& data)
        {
            AAsset* asset = AAssetManager_open(assetManager, url, AASSET_MODE_UNKNOWN);
            if (asset == nullptr)
            {
                throw std::runtime_error("Failed to open asset");
            }

            data.resize(AAsset_getLength64(asset));
            AAsset_read(asset, data.data(), data.size());
            AAsset_close(asset);
        }
    }

    class UrlRequest::Impl : public ImplBase
    {
    public:
        void Open(UrlMethod method, const std::string& url)
        {
            ResetForOpen();
            m_responseBuffer.clear();

            m_method = method;
            Uri uri{Uri::Parse(url.data())};
            // If the URL string doesn't contain a scheme, the URI object's scheme will be null. We throw in this case
            // The path is never null, even if it's empty it will be an empty string
            if ((jstring)uri.getScheme() == nullptr)
            {
                throw std::runtime_error("Cannot parse a URI without a scheme");
            }
            if ((std::string)uri.getScheme() == "app")
            {
                m_schemeIsApp = true;
                m_appPathOrUrl = uri.getPath();
            }
            else
            {
                // Platform API can handle both http:// and file:// schemes
                m_schemeIsApp = false;
                m_appPathOrUrl = std::move(url);
            }
        }

        arcana::task<void, std::exception_ptr> SendAsync()
        {
            return arcana::make_task(arcana::threadpool_scheduler, m_cancellationSource, [this]()
            {
                try
                {
                    if (m_schemeIsApp)
                    {
                        std::string path{m_appPathOrUrl.substr(1)};
                        AAssetManager* assetsManager{GetAppContext().getAssets()};

                        switch (m_responseType)
                        {
                            case UrlResponseType::String:
                            {
                                LoadAsset(assetsManager, path.data(), m_responseString);
                                break;
                            }
                            case UrlResponseType::Buffer:
                            {
                                LoadAsset(assetsManager, path.data(), m_responseBuffer);
                                break;
                            }
                            default:
                            {
                                throw std::runtime_error{"Invalid response type"};
                            }
                        }

                        m_statusCode = UrlStatusCode::Ok;
                    }
                    else
                    {
                        URL url{m_appPathOrUrl.data()};

                        URLConnection connection{url.OpenConnection()};

                        // set request headers
                        for (auto request : m_requestHeaders)
                        {
                            const std::string& key = request.first;
                            const std::string& value = request.second;
                            connection.SetRequestProperty(key, value);
                        }
                        m_requestHeaders.clear();

                        // if this a POST request
                        if (m_method == UrlMethod::Post)
                        {
                            ((HttpURLConnection)connection).SetRequestMethod("POST");
                            connection.SetDoOutput(true);

                            // need to manually set the content length of the request body
                            size_t numBytes = m_requestBody.size();
                            connection.SetRequestProperty("Content-Length", std::to_string(numBytes));

                            OutputStream outputStream{connection.GetOutputStream()};
                            OutputStreamWriter writer{outputStream};
                            writer.Write(m_requestBody);
                            writer.Close();
                        }

                        connection.Connect();
                        int statusCode = static_cast<int>(UrlStatusCode::Ok);
                        if (connection.GetClass().IsAssignableFrom(HttpURLConnection::Class()))
                        {
                            statusCode = ((HttpURLConnection)connection).GetResponseCode();
                        }

                        for (int n = 0;; ++n)
                        {
                            String key = connection.GetHeaderFieldKey(n);
                            String value = connection.GetHeaderField(n);
                            if ((jstring)key == nullptr || (jstring)value == nullptr)
                            {
                                break;
                            }

                            std::string lowerCaseKey = key;
                            ToLower(lowerCaseKey);

                            m_headers.insert({lowerCaseKey, value});
                        }

                        const int contentLength = connection.GetContentLength();
                        auto env = GetEnvForCurrentThread();
                        auto stream = GetResponseStream(connection, statusCode);
                        auto releaseStream = gsl::finally([&] { env->DeleteLocalRef(stream); });
                        ByteArrayOutputStream byteArrayOutputStream{std::max(contentLength, 0)};
                        ThrowIfFaulted(env);

                        ByteArray byteArray{4096};
                        ThrowIfFaulted(env);
                        size_t totalBytesRead{};
                        if (stream)
                        {
                            InputStream inputStream{stream};
                            while (true)
                            {
                                const int bytesRead = inputStream.Read(byteArray);
                                // The pinned InputStream wrapper leaves read exceptions pending.
                                // Check before making another JNI call or interpreting its result.
                                ThrowIfFaulted(env);
                                if (bytesRead == -1)
                                {
                                    break;
                                }
                                byteArrayOutputStream.Write(byteArray, 0, bytesRead);
                                ThrowIfFaulted(env);
                                totalBytesRead += static_cast<size_t>(bytesRead);
                            }
                        }
                        if (statusCode != 204 && statusCode != 304 &&
                            contentLength >= 0 && totalBytesRead != static_cast<size_t>(contentLength))
                        {
                            throw std::runtime_error{"Response body ended before Content-Length bytes were received"};
                        }

                        switch (m_responseType)
                        {
                            case UrlResponseType::String:
                            {
                                // TODO: use the charset from the content type?
                                auto text = byteArrayOutputStream.ToString("UTF-8");
                                ThrowIfFaulted(env);
                                m_responseString = text;
                                break;
                            }
                            case UrlResponseType::Buffer:
                            {
                                auto bytes = byteArrayOutputStream.ToByteArray();
                                ThrowIfFaulted(env);
                                m_responseBuffer = bytes;
                                break;
                            }
                            default:
                            {
                                throw std::runtime_error{"Invalid response type"};
                            }
                        }

                        // Must happen after getting the content to get the redirected URL.
                        m_responseUrl = connection.GetURL().ToString();
                        ThrowIfFaulted(env);
                        m_statusCode = static_cast<UrlStatusCode>(statusCode);
                    }
                }
                catch (const Throwable& error)
                {
                    ResetForOpen();
                    m_responseBuffer.clear();
                    SetError("java", "JavaException", 0, error.what());
                }
                catch (const std::exception& error)
                {
                    ResetForOpen();
                    m_responseBuffer.clear();
                    SetError("urllib", "ResponseReadFailed", 0, error.what());
                }
            });
        }

        gsl::span<const std::byte> ResponseBuffer() const
        {
            return m_responseBuffer;
        }

    private:
        bool m_schemeIsApp{};
        std::string m_appPathOrUrl{};
        std::vector<std::byte> m_responseBuffer{};
    };
}

#include "UrlRequest_Shared.h"
