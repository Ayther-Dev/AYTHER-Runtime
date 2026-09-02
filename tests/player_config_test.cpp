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
#include <system_error>

int main() {
    namespace fs = std::filesystem;
    std::printf("== player_config_test (#299) ==\n");

    int passed_checks{};
    int failed_checks{};
    const auto check = [&](bool condition, const char* description) {
        if (condition) {
            ++passed_checks;
        } else {
            ++failed_checks;
        }
        std::printf("  [%s] %s\n", condition ? " OK " : "FAIL", description);
    };

    std::error_code filesystem_error;
    const fs::path test_directory =
        fs::temp_directory_path() / "ayther_player_cfg_test";
    fs::remove_all(test_directory, filesystem_error);
    fs::create_directories(test_directory, filesystem_error);

    // -- 1. La clave -------------------------------------------------------
    std::printf("\n[1] una configuracion por juego Y pack\n");
    const auto primary_path = ayther::player_config_path(
        test_directory, "crc32:7b905383", "Golden Axe HD");
    const auto alternate_pack_path = ayther::player_config_path(
        test_directory, "crc32:7b905383", "Otro Pack");
    const auto alternate_game_path = ayther::player_config_path(
        test_directory, "crc32:aaaaaaaa", "Golden Axe HD");
    check(primary_path != alternate_pack_path,
          "el mismo juego con OTRO pack es otra configuracion");
    check(primary_path != alternate_game_path,
          "otro juego con el mismo pack, tambien");
    // Sin pack la clave es sólo el juego: una sesión sin remasterizar igual
    // guarda volumen y shaders.
    check(ayther::player_config_path(test_directory, "crc32:7b905383", "") !=
              primary_path,
          "sin pack la clave es solo el juego");

    // Un nombre de pack con caracteres que Windows no acepta en una ruta no
    // puede hacer que la configuracion deje de guardarse EN SILENCIO — que es
    // lo peor que puede hacer una preferencia.
    const auto sanitized_path = ayther::player_config_path(
        test_directory,
        "crc32:7b905383",
        "Golden Axe: HD/4K \"final\".");
    ayther::PlayerConfig sanitized_config;
    sanitized_config.profile = "enhanced";
    check(ayther::player_config_save(sanitized_path, sanitized_config),
          "un nombre con : / \" y punto final se guarda igual");
    check(ayther::player_config_load(sanitized_path).profile == "enhanced",
          "…y se vuelve a leer");
    check(sanitized_path.filename().string().find('/') == std::string::npos &&
              sanitized_path.filename().string().find(':') == std::string::npos,
          "…porque la clave se sanea");

    // -- 2. Ida y vuelta ----------------------------------------------------
    std::printf("\n[2] va y vuelve entero\n");
    ayther::PlayerConfig written_config;
    written_config.profile = "faithful";
    written_config.subsystems = 0b1011;
    written_config.have_subsystems = true;
    written_config.bus_gain[1] = 0.35F;
    written_config.bus_muted[2] = true;
    written_config.shaders_on = false;
    written_config.hd_on = true;
    check(ayther::player_config_save(primary_path, written_config),
          "se escribe");
    const auto loaded_config = ayther::player_config_load(primary_path);
    check(loaded_config.profile == "faithful", "el perfil");
    check(loaded_config.subsystems == 0b1011 &&
              loaded_config.have_subsystems,
          "la mascara de subsistemas");
    check(loaded_config.bus_gain[1] > 0.34F &&
              loaded_config.bus_gain[1] < 0.36F,
          "el volumen por bus");
    check(loaded_config.bus_muted[2] && !loaded_config.bus_muted[0],
          "el silencio por bus, sin contagiarse");
    check(!loaded_config.shaders_on && loaded_config.hd_on,
          "los dos toggles");

    // -- 3. La distincion que decide si el juego arranca remasterizado ------
    std::printf("\n[3] «apago todo» no es «nunca toco nada»\n");
    // Sin archivo: los defaults, y `have_subsystems` en falso — el runtime no
    // tiene que aplicar ninguna mascara.
    const auto missing_config =
        ayther::player_config_load(test_directory / "no-existe.toml");
    check(!missing_config.have_subsystems,
          "sin archivo: NO hay mascara guardada (el pack manda)");
    check(missing_config.hd_on && missing_config.shaders_on &&
              missing_config.profile.empty(),
          "…y los defaults son los del pack, no apagado");

    // Con archivo y mascara 0: el jugador apago todo A PROPOSITO, y eso hay que
    // respetarlo. Si esto se leyera como «nunca toco nada», el juego volveria a
    // arrancar remasterizado en cada partida y el ajuste no serviria.
    ayther::PlayerConfig disabled_config;
    disabled_config.subsystems = 0;
    disabled_config.have_subsystems = true;
    const auto disabled_config_path = test_directory / "todo_apagado.toml";
    ayther::player_config_save(disabled_config_path, disabled_config);
    const auto loaded_disabled_config =
        ayther::player_config_load(disabled_config_path);
    check(loaded_disabled_config.have_subsystems &&
              loaded_disabled_config.subsystems == 0,
          "guardar 0 se relee como «apagado a proposito», no como ausente");

    // -- 4. Robustez --------------------------------------------------------
    std::printf("\n[4] un archivo roto no tira nada\n");
    {
        const auto malformed_config_path = test_directory / "rota.toml";
        std::FILE* config_file =
            std::fopen(malformed_config_path.string().c_str(), "wb");
        if (config_file != nullptr) {
            std::fputs("esto no es toml\nsubsystems = pepe\n[[[\n", config_file);
            std::fclose(config_file);
        }
        const auto recovered_config =
            ayther::player_config_load(malformed_config_path);
        // Lo que importa no es qué valor sale sino que SALGA uno usable: una
        // preferencia corrupta no puede impedir jugar.
        check(recovered_config.bus_gain[0] >= 0.0F,
              "una configuracion ilegible da valores usables");
    }

    fs::remove_all(test_directory, filesystem_error);
    std::printf("\n%d passed, %d failed\n", passed_checks, failed_checks);
    return failed_checks == 0 ? 0 : 1;
}
