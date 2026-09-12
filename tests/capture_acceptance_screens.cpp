// Local acceptance helper. Requires an explicitly supplied, verified real core
// and ROM; writes only screen identities, never distributes game content.
#include <ayther/ayther_session.h>
#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <set>
#include <tuple>

int main(int argc, char** argv) {
    if (argc != 4) return 1;
    ayther::AytherSession::Config config;
    config.core_path = argv[1]; config.rom_path = argv[2];
    config.derive_core_pack = false; config.enable_audio = false;
    auto session = ayther::AytherSession::create(config);
    if (!session) return 2;
    std::ofstream output{argv[3]};
    if (!output) return 3;
    for (unsigned frame = 1; frame <= 240; ++frame) {
        const auto& view = (*session)->step();
        if (frame != 120 && frame != 240) continue;
        output << "\n[[screen]]\nid = \"0x" << std::hex << frame << std::dec
               << "\"\nname = \"Acceptance screen\"\nplanes = 7\nmin_match = 0.92\nmax_extra = 0.08\n"
                  "asset = \"video/acceptance.ivf\"\ncells = \"";
        bool first = true;
        std::set<std::tuple<unsigned, int, int>> cells;
        for (unsigned i = 0; i < view.plane_cell_count; ++i) {
            const auto& c = view.plane_cells[i];
            if (!c.hash || c.screen_x < 0 || c.screen_y < 0 ||
                c.screen_x % 8 || c.screen_y % 8 || c.plane > 2) continue;
            const int col = c.screen_x / 8, row = c.screen_y / 8;
            if (!cells.emplace(c.plane, col, row).second) continue;
            output << (first ? "" : "|") << "0x" << std::hex << c.hash << std::dec
                   << ':' << unsigned(c.plane) << ',' << col << ',' << row;
            first = false;
        }
        if (first) return 4;
        output << "\"\n";
    }
    output << "\n[[kinematic]]\nid = \"0x1234\"\nname = \"VP9 acceptance ONLY\"\ngap = 10\nloop = true\n"
              "steps = \"0x78:video/acceptance.ivf|0xf0:video/acceptance.ivf\"\n";
    return output ? 0 : 5;
}
