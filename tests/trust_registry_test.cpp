#include "trust_registry.h"
#include "session_controller.h"
#include <ayther/ayther_session.h>
#include <ayther/engine/pack.hpp>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#include <share.h>
#endif

namespace fs = std::filesystem;
int main(int argc, char** argv) {
    if (argc != 4) return 1;
    const fs::path fixtures{argv[1]};
    const fs::path scratch = fs::path{argv[3]} / std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(scratch);
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); } } cleanup{scratch};
    int failed = 0;
    const auto check = [&](const bool ok, const char* name) {
        std::printf("[%s] %s\n", ok ? "OK" : "FAIL", name);
        failed += !ok;
    };
    const auto config = ayther::runtime::resolve_trust_registry(fixtures / "valid.toml");
    check(config && fs::path{config.path}.is_absolute(), "registry resolves to an absolute path");
    check(!ayther::runtime::resolve_trust_registry(scratch / "missing.toml"), "missing registry");
    check(!ayther::runtime::resolve_trust_registry(scratch), "directory is not a registry");
#ifdef _WIN32
    const auto locked_path = (scratch / "locked.toml").string();
    std::ofstream{locked_path} << "version = 1\n";
    if (auto* locked = _fsopen(locked_path.c_str(), "rb", _SH_DENYRW)) {
        check(!ayther::runtime::resolve_trust_registry(locked_path), "unreadable registry (exclusive Windows lock)");
        std::fclose(locked);
    } else {
        check(false, "exclusive lock fixture could not be opened");
    }
#endif
    for (const auto* invalid : {"version = [", "version = 2", "version = 1.0", "version = 1\nkeys = 42",
                              "version = 1\nunknown = true", "version = 1\n[[keys]]\nid = 2"}) {
        std::ofstream{scratch / "invalid.toml"} << invalid;
        check(!ayther::runtime::resolve_trust_registry(scratch / "invalid.toml"), "invalid registry rejected");
    }
    check(static_cast<bool>(ayther::engine::inspect_pack(fixtures / "valid.ay", config.path)), "valid signature");
    for (const auto* registry : {"unknown.toml", "revoked.toml", "expired.toml", "future.toml", "wrong-game.toml"})
        check(!ayther::engine::inspect_pack(fixtures / "valid.ay", fixtures / registry), registry);
    for (const auto* pack : {"tampered.ay", "unsigned.ay", "development.ay"})
        check(!ayther::engine::inspect_pack(fixtures / pack, config.path), pack);
#ifdef NDEBUG
    check(!ayther::engine::inspect_pack(fixtures / "development.ay"), "Release refuses development signature without registry");
    check(!ayther::engine::inspect_pack(fixtures / "unsigned.ay"), "Release refuses unsigned pack without registry");
#endif
    const auto pack = scratch / "pack.ay";
    const auto registry = scratch / "trust.toml";
    fs::copy_file(fixtures / "valid.ay", pack);
    fs::copy_file(fixtures / "valid.toml", registry);
    std::ofstream{scratch / "fixture.rom"} << "TEST ROM";
    ayther::AytherSession::Config session_config;
    session_config.core_path = argv[2];
    session_config.rom_path = (scratch / "fixture.rom").string();
    session_config.pack_path = pack.string();
    session_config.trust_registry = ayther::runtime::resolve_trust_registry(registry).path;
    session_config.enable_audio = false;
    session_config.derive_core_pack = false;
    auto session = ayther::AytherSession::create(session_config);
    check(session && (*session)->has_pack(), "session activates signed pack");
    if (session && (*session)->has_pack()) {
        ayther::runtime::SessionController controller{std::move(*session)};
        check(controller.reload_pack(pack.string()) && controller->has_pack(), "reload preserves trust registry");
        fs::copy_file(fixtures / "revoked.toml", registry, fs::copy_options::overwrite_existing);
        check(!controller.reload_pack(pack.string()) && !controller->has_pack(), "reload rereads revocation and deactivates pack");
    }
    return failed ? 1 : 0;
}
