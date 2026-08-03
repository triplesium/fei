#pragma once

namespace fei {

class Cls;
class ContainerAdapter;

void register_container_methods(Cls& cls, const ContainerAdapter& adapter);

} // namespace fei
