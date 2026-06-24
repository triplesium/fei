#pragma once

#include "asset/loader.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <queue>

namespace fei {

class AssetServer;
template<typename T>
class ResRW;

class AssetLoadRequestSender {
  private:
    struct Request {
        std::function<void(AssetServer&)> process;
        std::function<void()> cancel;
    };

    std::mutex m_mutex;
    std::queue<Request> m_requests;
    bool m_accepting {true};

  public:
    AssetLoadRequestSender() = default;
    ~AssetLoadRequestSender();

    AssetLoadRequestSender(const AssetLoadRequestSender&) = delete;
    AssetLoadRequestSender& operator=(const AssetLoadRequestSender&) = delete;

    AssetLoadRequestSender(AssetLoadRequestSender&&) = delete;
    AssetLoadRequestSender& operator=(AssetLoadRequestSender&&) = delete;

    template<typename T>
    Result<Handle<T>, AssetLoadError> try_load(const AssetPath& path);

  private:
    friend class AssetLoadRequests;

    bool enqueue(Request request);
    void process(AssetServer& server);
    void close();
};

class AssetLoadRequests {
  private:
    std::shared_ptr<AssetLoadRequestSender> m_sender;

  public:
    AssetLoadRequests();
    ~AssetLoadRequests();

    AssetLoadRequests(const AssetLoadRequests&) = delete;
    AssetLoadRequests& operator=(const AssetLoadRequests&) = delete;

    AssetLoadRequests(AssetLoadRequests&& other) noexcept;
    AssetLoadRequests& operator=(AssetLoadRequests&& other) noexcept;

    std::shared_ptr<AssetLoadRequestSender> sender() const;

    template<typename T>
    Result<Handle<T>, AssetLoadError> try_load(const AssetPath& path);

    void process(AssetServer& server);
    void close();

    static void process_system(
        ResRW<AssetLoadRequests> requests,
        ResRW<AssetServer> server
    );
};

} // namespace fei
