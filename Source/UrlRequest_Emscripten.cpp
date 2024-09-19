#include "UrlRequest_Base.h"
#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace UrlLib {
class UrlRequest::Impl : public ImplBase {
public:
  ~Impl() { Cleanup(); }

  void Open(UrlMethod method, const std::string &url) {
    Cleanup();

    m_method = method;
    m_url = url;
  }

  arcana::task<void, std::exception_ptr> SendAsync() {
    switch (m_responseType) {
    case UrlResponseType::String: {
      auto retval = PerformFetchAsync(m_responseString);
      return retval;
    }
    case UrlResponseType::Buffer: {
      return PerformFetchAsync(m_responseBuffer);
    }
    }

    throw std::runtime_error{"Invalid response type"};
  }

  gsl::span<const std::byte> ResponseBuffer() const { return m_responseBuffer; }

private:
  void Cleanup() {
    if (m_thread.has_value()) {
      m_thread->join();
      m_thread = {};
    }

    m_responseBuffer.clear();
    m_responseString.clear();
  }

  template <typename DataT>
  arcana::task<void, std::exception_ptr> PerformFetchAsync(DataT &data) {
    data.clear();

    using TSCPair =
        std::pair<arcana::task_completion_source<void, std::exception_ptr>,
                  DataT *>;

    TSCPair *tsc_pair = new TSCPair(
        {arcana::task_completion_source<void, std::exception_ptr>()}, &data);

    // Launch the fetch request asynchronously
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);

    if (m_method == UrlMethod::Get) {
      strcpy(attr.requestMethod, "GET");
    } else if (m_method == UrlMethod::Post) {
      strcpy(attr.requestMethod, "POST");
      // Add additional POST handling logic if needed
      // Set the POST data, e.g., attr.requestData
      // attr.requestData = "your_post_data_here";
      // attr.requestDataSize = strlen(attr.requestData);
    }

    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = [](emscripten_fetch_t *fetch) {
      try {
        TSCPair *tsc_pair = (TSCPair *)fetch->userData;
        if constexpr (std::is_same_v<DataT, std::string>) {
          tsc_pair->second->assign(fetch->data,
                                   fetch->numBytes); // For string responses
        } else if constexpr (std::is_same_v<DataT, std::vector<std::byte>>) {
          tsc_pair->second->assign(
              reinterpret_cast<const std::byte *>(fetch->data),
              reinterpret_cast<const std::byte *>(fetch->data) +
                  fetch->numBytes); // For binary buffer
        }

        tsc_pair->first.complete();    // Complete the task
        emscripten_fetch_close(fetch); // Free memory associated with the fetch

        delete tsc_pair;
      } catch (const std::exception &exc) {
        std::cerr << exc.what() << std::endl;
      } catch (...) {
        std::cerr << "unknown exception" << std::endl;
      }
    };

    attr.onerror = [](emscripten_fetch_t *fetch) {
      TSCPair *tsc_pair = (TSCPair *)fetch->userData;
      // Completing the task with an error
      tsc_pair->first.complete(arcana::make_unexpected(
          std::make_exception_ptr(std::runtime_error("Fetch request failed"))));

      emscripten_fetch_close(fetch);
      delete tsc_pair;
    };

    attr.userData = tsc_pair;
    emscripten_fetch(&attr, m_url.c_str());
    return tsc_pair->first.as_task();
  }

  std::vector<std::byte> m_responseBuffer{};
  std::string m_url;
  bool m_file{};
  std::optional<std::thread> m_thread{};
};
} // namespace UrlLib

#include "UrlRequest_Shared.h"
