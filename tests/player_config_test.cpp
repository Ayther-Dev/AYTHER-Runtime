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
#include <fstream>
#include <string>
#include <system_error>

namespace {

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

}  // namespace

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
    written_config.output = "pixel\\perfect\n\"quoted\"";
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
    check(loaded_config.output == written_config.output,
          "las cadenas con escapes conservan un round-trip exacto");
    check(read_text(primary_path).find("format_version = 1") !=
              std::string::npos,
          "el formato persistido declara su version");

    // -- 3. La distincion que decide si el juego arranca remasterizado ------
    std::printf("\n[3] «apago todo» no es «nunca toco nada»\n");
    // Sin archivo: los defaults, y `have_subsystems` en falso — el runtime no
    // tiene que aplicar ninguna mascara.
    const auto missing_result = ayther::player_config_load_checked(
        test_directory / "no-existe.toml");
    const auto& missing_config = missing_result.config;
    check(missing_result.status == ayther::PlayerConfigLoadStatus::missing,
          "archivo ausente se distingue de archivo invalido");
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

    // -- 4. Gramatica exacta y diagnosticos --------------------------------
    std::printf("\n[4] claves, tipos y rangos son exactos\n");
    {
        const auto malformed_config_path = test_directory / "rota.toml";
        write_text(malformed_config_path,
                   "format_version = 1\nhd = false\nsubsystems = pepe\n");
        const std::string original_bytes = read_text(malformed_config_path);
        const auto invalid =
            ayther::player_config_load_checked(malformed_config_path);
        check(invalid.status == ayther::PlayerConfigLoadStatus::invalid &&
                  invalid.line == 3U && !invalid.diagnostic.empty(),
              "un numero invalido produce diagnostico con linea");
        check(invalid.config.hd_on,
              "un archivo invalido no publica valores parciales");
        check(read_text(malformed_config_path) == original_bytes,
              "leer un archivo corrupto no lo destruye ni reescribe");

        const auto prefix_path = test_directory / "prefijo.toml";
        write_text(prefix_path,
                   "format_version = 1\nhd_backup = false\n");
        const auto prefix = ayther::player_config_load_checked(prefix_path);
        check(prefix.loaded() && prefix.config.hd_on,
              "hd_backup no se interpreta como hd");

        const auto bool_path = test_directory / "bool.toml";
        write_text(bool_path, "format_version = 1\nshaders = yes\n");
        check(ayther::player_config_load_checked(bool_path).status ==
                  ayther::PlayerConfigLoadStatus::invalid,
              "un booleano no canonico es invalido");

        const auto gain_path = test_directory / "gain.toml";
        write_text(gain_path,
                   "format_version = 1\nbus_gain = [1, 1.5, 1, 1]\n");
        check(ayther::player_config_load_checked(gain_path).status ==
                  ayther::PlayerConfigLoadStatus::invalid,
              "un volumen fuera de [0,1] es invalido");

        const auto future_path = test_directory / "future.toml";
        write_text(future_path, "format_version = 2\nhd = false\n");
        check(ayther::player_config_load_checked(future_path).status ==
                  ayther::PlayerConfigLoadStatus::unsupported_version,
              "una version futura no se interpreta silenciosamente");

        const auto legacy_path = test_directory / "legacy-v0.toml";
        write_text(legacy_path, "profile = \"legacy\"\nhd = false\n");
        const auto legacy = ayther::player_config_load_checked(legacy_path);
        check(legacy.loaded() && legacy.config.profile == "legacy" &&
                  !legacy.config.hd_on,
              "un fixture legacy v0 conserva compatibilidad hacia atras");

        const auto transactional_path = test_directory / "transactional.toml";
        write_text(transactional_path, "format_version = 1\nprofile = \"old\"\n");
        ayther::PlayerConfig replacement;
        replacement.profile = "new";
        const std::string previous = read_text(transactional_path);
        check(!ayther::player_config_save(
                  transactional_path, replacement,
                  ayther::PlayerConfigSaveFault::disk_full) &&
                  read_text(transactional_path) == previous,
              "falta de espacio preserva la configuracion anterior");
        check(!ayther::player_config_save(
                  transactional_path, replacement,
                  ayther::PlayerConfigSaveFault::before_publish) &&
                  read_text(transactional_path) == previous &&
                  !fs::exists(transactional_path.string() + ".tmp"),
              "una escritura interrumpida no publica ni deja temporales");
    }

    fs::remove_all(test_directory, filesystem_error);
    std::printf("\n%d passed, %d failed\n", passed_checks, failed_checks);
    return failed_checks == 0 ? 0 : 1;
}
