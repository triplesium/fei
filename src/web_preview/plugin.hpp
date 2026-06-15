#pragma once
#include "app/plugin.hpp"
#include "base/types.hpp"

#include <string>

namespace fei {

enum class WebPreviewTarget : uint8 {
    Auto,
};

struct WebPreviewConfig {
    std::string host {"127.0.0.1"};
    uint16 port {8080};
    int jpeg_quality {80};
    WebPreviewTarget target {WebPreviewTarget::Auto};
};

class WebPreviewPlugin : public Plugin {
  public:
    explicit WebPreviewPlugin(WebPreviewConfig config = {});

    void setup(App& app) override;
    void finish(App& app) override;

  private:
    WebPreviewConfig m_config;
};

} // namespace fei
