#include <ayther/engine/capabilities.hpp>
#include <ayther/engine/pack.hpp>
#include <ayther/ayther_session.h>

#include <cstdio>
#include <type_traits>
#include <utility>
#include <vector>

static_assert(std::is_trivially_copyable_v<ayther::engine::PackView>);
static_assert(!std::is_copy_constructible_v<ayther::engine::PackWatcher>);
static_assert(std::is_nothrow_move_constructible_v<ayther::engine::PackWatcher>);
static_assert(std::is_same_v<
              decltype(std::declval<const ayther::AytherSession&>()
                           .pack_overlays()),
              const std::vector<ayther::AytherSession::PackOverlay>&>);

int main() {
    const ayther::engine::PackView empty_pack;
    if (empty_pack || !empty_pack.render_tiers().is_legacy()) {
        std::fprintf(stderr, "empty PackView contract failed\n");
        return 1;
    }

    if (ayther::engine::core_abi_revision() == 0U) {
        std::fprintf(stderr, "linked core ABI revision is unavailable\n");
        return 2;
    }

    const auto inspection = ayther::engine::inspect_pack("mig027-pack-that-does-not-exist.ay");
    if (inspection || inspection.error.code != ayther::ErrorCode::NotFound) {
        std::fprintf(stderr, "typed inspection did not report NotFound\n");
        return 3;
    }

    const auto watcher = ayther::engine::PackWatcher::create({});
    if (watcher || watcher.error.code != ayther::ErrorCode::NotFound) {
        std::fprintf(stderr, "typed watcher accepted an empty path\n");
        return 4;
    }
    return 0;
}
