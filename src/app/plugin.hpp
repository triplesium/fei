#pragma once

#include "app/app.hpp"
namespace fei {

class Plugin {
  public:
    virtual ~Plugin() = default;
    virtual void setup(App& app) = 0;
};

} // namespace fei
