#pragma once

namespace fei {

class App;

class Plugin {
  public:
    virtual ~Plugin() = default;
    virtual void setup(App& app) = 0;
    virtual void finish(App& app) {}
};

} // namespace fei
