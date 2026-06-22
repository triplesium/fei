#include "asset/request.hpp"

#include "asset/server.hpp"
#include "ecs/system_params.hpp"

#include <utility>

namespace fei {

AssetLoadRequestSender::~AssetLoadRequestSender() {
    close();
}

bool AssetLoadRequestSender::enqueue(Request request) {
    std::scoped_lock lock(m_mutex);
    if (!m_accepting) {
        return false;
    }
    m_requests.push(std::move(request));
    return true;
}

void AssetLoadRequestSender::process(AssetServer& server) {
    std::queue<Request> requests;
    {
        std::scoped_lock lock(m_mutex);
        requests.swap(m_requests);
    }

    while (!requests.empty()) {
        requests.front().process(server);
        requests.pop();
    }
}

void AssetLoadRequestSender::close() {
    std::queue<Request> requests;
    {
        std::scoped_lock lock(m_mutex);
        m_accepting = false;
        requests.swap(m_requests);
    }

    while (!requests.empty()) {
        requests.front().cancel();
        requests.pop();
    }
}

AssetLoadRequests::AssetLoadRequests() :
    m_sender(std::make_shared<AssetLoadRequestSender>()) {}

AssetLoadRequests::~AssetLoadRequests() {
    close();
}

AssetLoadRequests::AssetLoadRequests(AssetLoadRequests&& other) noexcept :
    m_sender(std::move(other.m_sender)) {}

AssetLoadRequests&
AssetLoadRequests::operator=(AssetLoadRequests&& other) noexcept {
    if (this != &other) {
        close();
        m_sender = std::move(other.m_sender);
    }
    return *this;
}

std::shared_ptr<AssetLoadRequestSender> AssetLoadRequests::sender() const {
    return m_sender;
}

void AssetLoadRequests::process(AssetServer& server) {
    if (m_sender) {
        m_sender->process(server);
    }
}

void AssetLoadRequests::close() {
    if (m_sender) {
        m_sender->close();
    }
}

void AssetLoadRequests::process_system(
    Res<AssetLoadRequests> requests,
    Res<AssetServer> server
) {
    requests->process(*server);
}

} // namespace fei
