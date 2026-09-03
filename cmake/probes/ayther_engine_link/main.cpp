#include <ayther/engine/capabilities.hpp>

int main() {
    const auto linked_engine = ayther::engine::version();
    return linked_engine.major == 0xffffu ? 1 : 0;
}
