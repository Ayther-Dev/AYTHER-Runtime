// ---------------------------------------------------------------------------
// player_config_test.cpp — #299: lo que el jugador ajusta y se recuerda.
//
// El panel in-game necesita ventana, gamepad y Vulkan. Lo que SÍ se puede fijar
// sin nada de eso es la parte que decide si la configuración sirve:
//
//   · la CLAVE — por juego + pack, y por el nombre del pack y no su build id;
//   · la distinción entre «el jugador apagó todo» y «nunca tocó nada», que un
//     0 no puede codificar y que decide si el juego arranca remasterizado;
//   · que un nombre de pack con caracteres raros no rompa la ruta en silencio.
// ---------------------------------------------------------------------------
#include "player_config.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {

int g_pass = 0, g_fail = 0;
void check(bool ok, const char* what) {
    if (ok) ++g_pass; else ++g_fail;
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    std::printf("== player_config_test (#299) ==\n");

    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "ayther_player_cfg_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // -- 1. La clave -------------------------------------------------------
    std::printf("\n[1] una configuracion por juego Y pack\n");
    const auto a = ayther::player_config_path(dir, "crc32:7b905383", "Golden Axe HD");
    const auto b = ayther::player_config_path(dir, "crc32:7b905383", "Otro Pack");
    const auto c = ayther::player_config_path(dir, "crc32:aaaaaaaa", "Golden Axe HD");
    check(a != b, "el mismo juego con OTRO pack es otra configuracion");
    check(a != c, "otro juego con el mismo pack, tambien");
    // Sin pack la clave es sólo el juego: una sesión sin remasterizar igual
    // guarda volumen y shaders.
    check(ayther::player_config_path(dir, "crc32:7b905383", "") != a,
          "sin pack la clave es solo el juego");

    // Un nombre de pack con caracteres que Windows no acepta en una ruta no
    // puede hacer que la configuracion deje de guardarse EN SILENCIO — que es
    // lo peor que puede hacer una preferencia.
    const auto raro = ayther::player_config_path(dir, "crc32:7b905383",
                                                 "Golden Axe: HD/4K \"final\".");
    ayther::PlayerConfig cr;
    cr.profile = "enhanced";
    check(ayther::player_config_save(raro, cr), "un nombre con : / \" y punto final se guarda igual");
    check(ayther::player_config_load(raro).profile == "enhanced", "…y se vuelve a leer");
    check(raro.filename().string().find('/') == std::string::npos
              && raro.filename().string().find(':') == std::string::npos,
          "…porque la clave se sanea");

    // -- 2. Ida y vuelta ----------------------------------------------------
    std::printf("\n[2] va y vuelve entero\n");
    ayther::PlayerConfig w;
    w.profile         = "faithful";
    w.subsystems      = 0b1011;
    w.have_subsystems = true;
    w.bus_gain[1]     = 0.35f;
    w.bus_muted[2]    = true;
    w.shaders_on      = false;
    w.hd_on           = true;
    check(ayther::player_config_save(a, w), "se escribe");
    const auto r = ayther::player_config_load(a);
    check(r.profile == "faithful",        "el perfil");
    check(r.subsystems == 0b1011 && r.have_subsystems, "la mascara de subsistemas");
    check(r.bus_gain[1] > 0.34f && r.bus_gain[1] < 0.36f, "el volumen por bus");
    check(r.bus_muted[2] && !r.bus_muted[0], "el silencio por bus, sin contagiarse");
    check(!r.shaders_on && r.hd_on,       "los dos toggles");

    // -- 3. La distincion que decide si el juego arranca remasterizado ------
    std::printf("\n[3] «apago todo» no es «nunca toco nada»\n");
    // Sin archivo: los defaults, y `have_subsystems` en falso — el runtime no
    // tiene que aplicar ninguna mascara.
    const auto virgen = ayther::player_config_load(dir / "no-existe.toml");
    check(!virgen.have_subsystems,
          "sin archivo: NO hay mascara guardada (el pack manda)");
    check(virgen.hd_on && virgen.shaders_on && virgen.profile.empty(),
          "…y los defaults son los del pack, no apagado");

    // Con archivo y mascara 0: el jugador apago todo A PROPOSITO, y eso hay que
    // respetarlo. Si esto se leyera como «nunca toco nada», el juego volveria a
    // arrancar remasterizado en cada partida y el ajuste no serviria.
    ayther::PlayerConfig off;
    off.subsystems = 0;
    off.have_subsystems = true;
    const auto pOff = dir / "todo_apagado.toml";
    ayther::player_config_save(pOff, off);
    const auto rOff = ayther::player_config_load(pOff);
    check(rOff.have_subsystems && rOff.subsystems == 0,
          "guardar 0 se relee como «apagado a proposito», no como ausente");

    // -- 4. Robustez --------------------------------------------------------
    std::printf("\n[4] un archivo roto no tira nada\n");
    {
        const auto rota = dir / "rota.toml";
        std::FILE* f = std::fopen(rota.string().c_str(), "wb");
        if (f) { std::fputs("esto no es toml\nsubsystems = pepe\n[[[\n", f); std::fclose(f); }
        const auto rr = ayther::player_config_load(rota);
        // Lo que importa no es qué valor sale sino que SALGA uno usable: una
        // preferencia corrupta no puede impedir jugar.
        check(rr.bus_gain[0] >= 0.0f, "una configuracion ilegible da valores usables");
    }

    fs::remove_all(dir, ec);
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
