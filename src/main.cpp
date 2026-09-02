#include <SDL3/SDL.h>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <filesystem>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <cstdlib>                   // std::atoi (--frames), std::abort (--crash-test)
#include <cinttypes>
#include <ctime>
#include "runtime_config.h"
#include <ayther/ayther_session.h>    // the motor facade (R2.3) — replaces direct host/audio
#include <ayther/engine/capabilities.hpp>
#include <ayther/engine/pack.hpp>
#include "game_input.h"              // SDL keyboard/gamepad → RetroPad bitfield (M3)
#include "vulkan_backend/aspect_fit.h"  // 4:3 canvas fit (pillarbox, no stretch)
#include "player_overlay.h"         // in-game pause menu + HD↔Original toggle (M4)
#include "player_config.h"          // #299: lo que el panel ajusta y se recuerda
#include "capture.h"                // #300: captura comparativa sincronizada
#include <ayther/engine/core_probe.hpp>  // MIG-022: sondeo RAII publico del Engine
#include "output_profile.h"         // #296: perfiles de salida (CRT/LCD/pixel)
#include "diagnostics.h"            // #302: asistente de diagnostico
#include "vulkan_backend/vk_context.h"
#include "vulkan_backend/vk_swapchain.h"
#include "vulkan_backend/vk_present.h"
#include "vulkan_backend/vk_postprocess.h"   // CRT presentation pass (samples the offscreen)
#include <ayther/ayther_renderer.h>    // the motor's HD render layer (R3.1)
#include "pack_layers.h"           // #561: el stack de Acetatos del pack
#include "sonic_telemetry.h"       // Runtime-owned game-specific diagnostics
#include "version_info.h"          // Runtime build version + linked Engine version
// (vk_postprocess + the HD-tile cache + emu texture moved into the renderer;
//  capture.cpp owns Runtime's private stb PNG-writer implementation.)

// ---------------------------------------------------------------------------
// TileTexCache + the emu-frame texture + the HD render pipeline now live in the
// motor's AytherRenderer (engine/). main.cpp just drives renderer.render() and
// blits the offscreen result to the swapchain.
// ---------------------------------------------------------------------------


int main(int argc, char* argv[]) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    const std::string version_report = ayther::runtime::version_report();
    std::fprintf(stdout, "[main] %s\n", version_report.c_str());

    // #627 — el cronometro del arranque. Entre apretar «Jugar» y ver el juego
    // pasaban MINUTOS y no habia forma de saber en cual de los diez tramos: la
    // sesion imprime lo que hizo, no cuanto tardo. Con esto, el log de una
    // sesion lenta dice donde se fue el tiempo — que es la unica manera de
    // arreglarlo en la maquina de otro.
    const auto startup_begin_time = std::chrono::steady_clock::now();
    const auto log_startup_milestone =
        [startup_begin_time](const char* milestone) {
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - startup_begin_time)
                    .count();
            std::fprintf(stdout, "[tiempo] %6lld ms  %s\n",
                         static_cast<long long>(elapsed_ms), milestone);
        };

    // Resolve Runtime-owned storage before core probing or SDL initialization.
    // Engine authoring configuration is intentionally outside this process.
    const ayther::runtime::RuntimePaths runtime_paths =
        ayther::runtime::RuntimePaths::discover();
    std::fprintf(stdout, "[Config] Runtime data: %s\n",
                 runtime_paths.user_data_directory().string().c_str());

    // SDL se inicializa DESPUES de leer los argumentos (mas abajo, junto a la
    // ventana). Estaba aca arriba, y eso le cobraba a `--probe-core` y a
    // `--help` una inicializacion de video + audio + gamepad que no usan: el
    // sondeo de un core solo hace LoadLibrary y lee dos simbolos. En una sesion
    // no interactiva —o con un driver de audio o video que enumera despacio—
    // ese init tarda MINUTOS, y Play sondea el core en el paso 4 de su wizard:
    // el launcher quedaba congelado eligiendo un archivo que ni siquiera hacia
    // falta abrir con SDL.

    // -----------------------------------------------------------------------
    // Headless host: configuration comes from the command line. The Rust Ayther
    // Play launcher (M5) spawns us with these flags; for dev you pass them
    // directly. There is no GUI launcher here — that is the launcher's job.
    //   ayther_runtime --core <libretro.dll> --rom <rom> [--pack <ay>] [--patch <ips/bps>]
    //                  [--profile <id>]
    // Back-compat: `ayther_runtime <core> <rom>` (positional) still works.
    // -----------------------------------------------------------------------
    std::string core_path_str, rom_path_str, pack_path_arg, patch_path_arg;
    // #293: perfil pedido al arrancar. Vacio = el predeterminado del pack.
    // Es lo que satisface «Play puede seleccionar el perfil antes de iniciar»:
    // Play spawnea este proceso, asi que su selector termina siendo esta flag.
    std::string profile_arg;
    // #603: donde viven los guardados, y con que ROM se hizo esta partida.
    //
    // La raiz la manda PLAY y no se deduce del arbol del Runtime: eran dos
    // carpetas distintas —el Runtime escribia en la suya— asi que Play no veia
    // los guardados que el juego generaba, ni podia contarlos ni sincronizarlos.
    // Un guardado que el jugador tiene y su launcher no encuentra es un
    // guardado perdido.
    std::string saves_dir_arg;
    std::string rom_crc32_arg;
    // #604: desde que guardado reanudar. Vacio = partida limpia.
    //
    // Es una RUTA y no una bandera «reanuda si podes»: quien elige cual de las
    // versiones se carga es Play, que es el unico que las tiene listadas y el
    // unico que le pregunto al jugador. El Runtime carga la que le dan o
    // ninguna.
    std::string load_state_arg;
    /// EM-7.1: opciones del core que pidio el llamador (Play, o la linea de
    /// comandos). Se aplican al crear la sesion — el core las lee una vez.
    std::vector<std::pair<std::string, std::string>> core_opts_arg;
    int  subsystems_arg = 0; bool have_subsystems_arg = false;
    int  mute_buses_arg = 0; bool have_mute_arg = false;
    int  shaders_arg    = -1;   // -1 = no se pidio nada
    std::string output_arg;   // #296: perfil de SALIDA pedido
    int  frames_limit = 0;       // --frames N: run N frames then exit cleanly (0 = unlimited)
    std::vector<int> capture_at; // --capture-at N[,M…]: captura A/B (como F12) al llegar a esos frames (dev/CI)
    bool crash_test   = false;   // --crash-test: std::abort() right after "ready" (IPC test)
    std::string probe_core;      // --probe-core <dll>: sondea un core y sale (PLAY-EP01.2, #589)
    std::string manifest_path;   // --manifest <toml>: el launch.toml de la sesion (PLAY-EP03.2, #596)
    bool hd_compose   = false;   // --hd-compose: retirado en #345, se acepta y avisa.
                                 // Opt-in: cuesta un 2º render/frame (perf de Play).
    {
        std::vector<std::string> pos;
        for (int i = 1; i < argc; ++i) {
            const std::string a = argv[i];
            auto value = [&](const char* flag) -> std::string {
                if (i + 1 >= argc) {
                    std::fprintf(stderr, "[ayther_runtime] %s needs a value\n", flag);
                    return std::string();
                }
                return argv[++i];
            };
            if      (a == "--core")  core_path_str  = value("--core");
            else if (a == "--rom")   rom_path_str   = value("--rom");
            else if (a == "--pack")  pack_path_arg  = value("--pack");
            else if (a == "--patch") patch_path_arg = value("--patch");
            else if (a == "--profile") profile_arg  = value("--profile");
            else if (a == "--saves-dir") saves_dir_arg = value("--saves-dir");
            else if (a == "--load-state") load_state_arg = value("--load-state");
            else if (a == "--rom-crc32") rom_crc32_arg = value("--rom-crc32");
            // #311: lo que Play decide ANTES de lanzar. Se aplican DESPUES de
            // la configuracion guardada (#299) porque son mas especificos:
            // el jugador acaba de elegirlos en la pantalla previa.
            else if (a == "--subsystems") { subsystems_arg = std::atoi(value("--subsystems").c_str()); have_subsystems_arg = true; }
            else if (a == "--mute-buses") { mute_buses_arg = std::atoi(value("--mute-buses").c_str()); have_mute_arg = true; }
            else if (a == "--output")  output_arg = value("--output");
            // EM-7.1 (#230): opciones del CORE, `clave=valor`. Repetible.
            // Son las que dan «sin limite de sprites» —el anti-flicker— y el
            // overclock donde el core los ofrezca. Que exista cada una depende
            // del core (BYOC), asi que el runtime no valida la clave: la pasa
            // y el core decide. Una clave que el core no conoce simplemente no
            // se pregunta nunca.
            else if (a == "--core-option") {
                const std::string v = value("--core-option");
                const size_t eq = v.find('=');
                // Sin `=` no es una opcion: se avisa en vez de guardar una
                // clave con valor vacio, que el core leeria como una eleccion.
                if (eq == std::string::npos || eq == 0)
                    std::fprintf(stderr,
                        "[main] --core-option espera clave=valor, no '%s'\n", v.c_str());
                else
                    core_opts_arg.emplace_back(v.substr(0, eq), v.substr(eq + 1));
            }
            // #596: el manifest de la sesion. El runtime NO lo parsea: los
            // valores llegan igual como argumentos, derivados de ESE mismo
            // archivo por Play. Se recibe y se ANOTA porque quien investiga un
            // crash necesita el hilo entre el proceso y su sesion — sin esto,
            // un `launch.toml` en el disco y un runtime que se colgo son dos
            // cosas que nadie puede unir.
            //
            // Leerlo aca seria duplicar el derivador de argumentos en C++ y en
            // Rust, y esas dos copias se separan: la que se olvide de una
            // opcion la ignora en silencio. Una sola fuente, un solo traductor.
            else if (a == "--manifest") manifest_path = value("--manifest");
            else if (a == "--no-shaders")   shaders_arg = 0;
            else if (a == "--shaders")      shaders_arg = 1;
            // dev/CI hooks (used by the launcher slice + headless tests):
            else if (a == "--frames")     frames_limit = std::atoi(value("--frames").c_str());
            else if (a == "--capture-at") {
                const std::string v = value("--capture-at");
                for (size_t p = 0; p < v.size();) {
                    size_t q = v.find(',', p); if (q == std::string::npos) q = v.size();
                    capture_at.push_back(std::atoi(v.substr(p, q - p).c_str()));
                    p = q + 1;
                }
            }
            else if (a == "--crash-test") crash_test   = true;
            else if (a == "--probe-core") probe_core   = value("--probe-core");
            else if (a == "--hd-compose") hd_compose   = true;
            else                     pos.push_back(a);
        }
        if (core_path_str.empty() && pos.size() >= 1) core_path_str = pos[0];
        if (rom_path_str.empty()  && pos.size() >= 2) rom_path_str  = pos[1];
    }
    const ayther::runtime::RuntimeConfig runtime_config(
        runtime_paths, std::filesystem::path{saves_dir_arg});
    // #230 EM-7.4: el parche del usuario. Deja de ser un `(void)`: se aplica
    // al buffer de la ROM en `RetroRunner::load_rom`, nunca al archivo.

    // -----------------------------------------------------------------------
    // --probe-core (PLAY-EP01.2, #589): decir QUE es este archivo y salir.
    //
    // Play no puede hacerlo por su cuenta: cargar un core libretro es cargar
    // codigo GPL en su proceso, que es justo la frontera que el ecosistema no
    // cruza (spec de producto de Play, §7). Asi que lo carga QUIEN ya lo carga
    // —este proceso—, publica el resultado por el mismo canal AYTHER_STATUS que
    // el resto del ciclo de vida, y se va. Si el archivo no es un core o es de
    // otra arquitectura, el que muere es este proceso desechable y Play se
    // entera por el codigo de salida, en vez de llevarse el launcher puesto.
    // -----------------------------------------------------------------------
    if (!probe_core.empty()) {
        auto probed = ayther::engine::probe_core(probe_core);
        if (!probed) {
            const bool invalid_core =
                probed.error.code == ayther::ErrorCode::BadFormat;
            std::fprintf(stderr, "[core-probe] %s\n",
                         probed.error.message.c_str());
            std::fprintf(stdout,
                "AYTHER_STATUS {\"event\":\"probe\",\"ok\":false,\"reason\":\"%s\"}\n",
                invalid_core ? "no_es_libretro" : "no_carga");
            std::fflush(stdout);
            return invalid_core ? 3 : 2;
        }

        // Engine serializa todos los valores que entrega el core. Runtime solo
        // agrega el framing de proceso que consume Play; no interpreta ni
        // conserva punteros Libretro prestados.
        std::string payload = probed->serialize();
        payload.insert(1, "\"event\":\"probe\",\"ok\":true,");
        std::fprintf(stdout, "AYTHER_STATUS %s\n", payload.c_str());
        std::fflush(stdout);
        return 0;
    }

    if (core_path_str.empty() || rom_path_str.empty()) {
        std::fprintf(stderr,
            "ayther_runtime — the Ayther game-session host (spawned by Ayther Play).\n"
            "Usage: ayther_runtime --core <libretro.dll> --rom <rom>\n"
            "         [--pack <ay>] [--patch <ips/bps>] [--profile <id>]\n"
            "         [--subsystems <mask>] [--mute-buses <mask>] [--output <id>]\n"
            "         [--core-option key=value]... [--no-shaders|--shaders]\n"
            "         [--manifest <launch.toml>] [--saves-dir <dir>]\n"
            "         [--rom-crc32 <hex>] [--load-state <estado.bin>]\n"
            "         [--frames N] [--capture-at N[,M...]] [--crash-test]\n"
            "         [--hd-compose]  (retirado en #345: se acepta y avisa)\n"
            "       ayther_runtime --probe-core <libretro.dll>\n"
            "         Sondea el core y emite AYTHER_STATUS probe. Sin ROM.\n");
        return 1;
    }

    const char* core_path = core_path_str.c_str();
    const char* rom_path  = rom_path_str.c_str();

    // -----------------------------------------------------------------------
    // SDL3 init — video, gamepad y audio de la sesion. Recien aca: los caminos
    // que salen antes (`--probe-core`, el uso) no tocan SDL.
    // -----------------------------------------------------------------------
    log_startup_milestone("argumentos leidos");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init(video) failed: %s\n", SDL_GetError());
        return 1;
    }
    log_startup_milestone("SDL video");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init(gamepad) failed: %s\n", SDL_GetError());
        return 1;
    }
    log_startup_milestone("SDL gamepad");
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "SDL_Init(audio) failed: %s\n", SDL_GetError());
        return 1;
    }
    log_startup_milestone("SDL audio");

    // La sesion abre a PANTALLA COMPLETA (#627). Es un reproductor de juegos
    // que se usa desde el sillon: una ventana de 1280x720 en una esquina del
    // escritorio es la pantalla de un emulador, no la de una consola. En SDL3
    // `SDL_WINDOW_FULLSCREEN` sin modo pedido es el fullscreen «de escritorio»
    // —sin cambio de modo de video, sin parpadeo al entrar y salir—, que es el
    // que no pelea con el compositor cuando Play vuelve a estar adelante.
    //
    // Sigue siendo RESIZABLE: al salir de pantalla completa (Alt+Enter del
    // gestor de ventanas) la ventana tiene que poder acomodarse.
    const std::string vulkan_window_title =
        ayther::runtime::window_title("Vulkan Passthrough");
    SDL_Window* window = SDL_CreateWindow(
        vulkan_window_title.c_str(),
        1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN
    );
    const bool vulkan_window = (window != nullptr);
    log_startup_milestone("ventana");

    if (!vulkan_window) {
        std::fprintf(stdout,
            "[main] SDL_WINDOW_VULKAN unavailable (%s) — falling back to headless\n",
            SDL_GetError());
        const std::string headless_window_title =
            ayther::runtime::window_title("Headless fallback");
        window = SDL_CreateWindow(
            headless_window_title.c_str(),
            1280, 720,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN
        );
        if (!window) {
            std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
    }

    // -----------------------------------------------------------------------
    // Rust ayther_core sanity check
    // -----------------------------------------------------------------------
    std::fprintf(stdout, "[main] ayther_core version: %u\n",
                 ayther::engine::core_abi_revision());

    // -----------------------------------------------------------------------
    // Vulkan context
    // -----------------------------------------------------------------------
    VkContext vulkan;
    const bool has_vulkan = vulkan_window && vulkan.init(window);
    log_startup_milestone("Vulkan");
    if (!has_vulkan) {
        std::fprintf(stdout,
            "[main] Vulkan context not available — running in Phase 1/2 mode\n");
    }

    // -----------------------------------------------------------------------
    // Swapchain + texture
    // -----------------------------------------------------------------------
    VkSwapchain swapchain;
    // Genesis framebuffer max size: 320×240 (Mode 5) — used by the sprite overlay.
    static constexpr uint32_t kEmuW = 320, kEmuH = 240;
    bool swap_ok = false;
    if (has_vulkan) {
        int win_w = 1280, win_h = 720;
        SDL_GetWindowSizeInPixels(window, &win_w, &win_h);
        swap_ok = swapchain.init(vulkan,
                                 static_cast<uint32_t>(win_w),
                                 static_cast<uint32_t>(win_h));
        if (!swap_ok) {
            std::fprintf(stderr, "[main] Swapchain init failed — headless fallback\n");
        }
    }

    // -----------------------------------------------------------------------
    // The motor — one AytherSession owns the emulator host + tile/sprite/audio
    // hashing + substitution + Lua script + HD pack + audio output, all behind
    // RAII. Replaces the ~9 hand-wired ayther_core handles + the inline video/
    // audio callbacks the player used to manage here (R2.3).
    // -----------------------------------------------------------------------

    // HD pack path — explicit --pack (the launcher passes it), else the dev
    // convention "<core stem>.ay" next to the core DLL (legacy .ae fallback,
    // pre-rebrand). Kept here because the Runtime-owned reload policy and its
    // typed watcher share the exact path.
    std::string pack_path = pack_path_arg;
    if (pack_path.empty()) {
        const auto dot  = core_path_str.rfind('.');
        const auto stem = (dot != std::string::npos)
                            ? core_path_str.substr(0, dot) : core_path_str;
        pack_path = stem + ".ay";
        if (!std::filesystem::exists(pack_path) &&
            std::filesystem::exists(stem + ".ae"))
            pack_path = stem + ".ae";
    }

    // #295: validar el pack ANTES de armar la sesión.
    //
    // Con errores se arranca SIN pack en vez de abortar: el juego original
    // tiene que correr igual. Un pack de otro juego no puede dejar al usuario
    // sin poder jugar — y decirlo por qué es la mitad del arreglo, porque el
    // que descarga un pack equivocado no tiene forma de saberlo mirando.
    //
    // Las advertencias se imprimen y se sigue: son degradación opcional (un
    // subsistema que este build no conoce, un core distinto del que lo horneó).
    bool pack_rejected = false;
    if (!pack_path.empty() && std::filesystem::exists(pack_path)) {
        ayther::engine::PackValidationContext validation_context;
        // The session does not exist yet, so platform/core/ROM remain unknown
        // and the validator reports them as unverified.
#ifdef NDEBUG
        validation_context.release_build = true;
#endif
        const auto validation = ayther::engine::validate_pack(
            pack_path, validation_context);
        if (!validation) {
            std::fprintf(stderr, "[pack] validation failed: %s\n",
                         validation.error.message.c_str());
            pack_path.clear();
            pack_rejected = true;
        } else {
            for (const auto& finding : validation->findings) {
                const bool is_error = finding.is_error();
                std::fprintf(is_error ? stderr : stdout,
                             "[pack] %s [%s] %s\n",
                             is_error ? "ERROR" : "aviso",
                             finding.code.empty() ? "?" : finding.code.c_str(),
                             finding.message.c_str());
            }
            if (validation->has_errors()) {
                std::fprintf(stderr,
                    "[pack] el pack NO se carga por lo de arriba — el juego "
                    "corre igual, en original\n");
                pack_path.clear();
                pack_rejected = true;
            }
        }
    }

    ayther::AytherSession::Config scfg;
    scfg.core_path    = core_path_str;
    scfg.rom_path     = rom_path_str;
    scfg.pack_path    = pack_path;     // explicit → watcher shares this exact path
    scfg.enable_audio = true;
    // Si la validación descartó el pack, NO buscar otro por convención. Sin
    // esto, rechazar un pack incompatible caía en el `<core>.ay` de al lado y
    // la sesión arrancaba con un pack DISTINTO del que el usuario pidió, sin
    // decirlo — medido: `has_pack=1` después de un rechazo. Es peor que no
    // cargar ninguno, porque parece que el rechazo no pasó.
    if (pack_rejected) scfg.derive_core_pack = false;
    // EM-7.1: las opciones del core van en el Config y no en un setter porque
    // el core las lee UNA VEZ, al inicializar. Aplicarlas despues compilaria y
    // no haria nada.
    scfg.core_options = core_opts_arg;
    scfg.patch_path   = patch_path_arg;   // #230 EM-7.4

    auto sess_r = ayther::AytherSession::create(scfg);
    if (!sess_r) {
        std::fprintf(stderr, "[main] AytherSession::create failed: %s\n",
                     sess_r.error.message.c_str());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::unique_ptr<ayther::AytherSession>& sess = *sess_r;
    sess->enable_rewind(true, 10);   // R6: 10 s rewind window (Backspace to rewind)

    // #293: el perfil pedido. Sin `--profile`, el pack ya arrancó en el suyo
    // (lo aplica `set_pack`), así que acá sólo hay que atender el caso de que
    // se haya pedido uno.
    //
    // Un id que no existe NO se aproxima ni se ignora en silencio: se avisa y
    // se sigue con el predeterminado. Arrancar en otra cosa sin decirlo haría
    // que el usuario juzgara la remasterización por un perfil que no eligió.
    if (!profile_arg.empty()) {
        if (!sess->set_profile(profile_arg)) {
            std::fprintf(stderr,
                "[main] el pack no tiene el perfil '%s' — se usa el predeterminado.\n"
                "       disponibles:", profile_arg.c_str());
            for (uint32_t i = 0; i < sess->profile_count(); ++i)
                std::fprintf(stderr, " %s", sess->profile_id(i).c_str());
            std::fprintf(stderr, "\n");
        }
    }
    std::fprintf(stdout, "[main] Session ready. game_id='%s'  has_pack=%d  profile='%s'\n",
                 sess->game_id(), static_cast<int>(sess->has_pack()),
                 sess->active_profile().c_str());

    // Audios (C-A4 paso 3): sustitución de audio EN VIVO desde el pack. Carga las
    // asignaciones firma→asset(+canales) del audio_events.toml del .ay y enciende
    // la sustitución por evento en tiempo real (mute selectivo de canales + HD en
    // sync). Sólo si el pack trae eventos — si no, el detector live no corre.
    sess->load_audio_events_from_pack();
    if (sess->audio_event_assignment_count() > 0) {
        sess->set_audio_runtime_substitution(true);
        std::fprintf(stdout, "[main] audio: %u evento(s) HD del pack → sustitución en vivo ON\n",
                     sess->audio_event_assignment_count());
    }

    // #345: `--hd-compose` encendía el compose por SUPRESIÓN (render B con el
    // original suprimido). Esa maquinaria murió con R-5 —el z-order lo da el
    // pase pri-1 de la escena— y se borró con la issue. El flag se sigue
    // ACEPTANDO para no romper un script que lo pase, pero avisa que ya no
    // hace nada en vez de fingir que hizo algo.
    if (hd_compose)
        std::fprintf(stdout, "[main] --hd-compose: retirado (#345) — el z-order "
                             "del HD lo da el pase pri-1 de la escena, sin "
                             "render extra\n");

    // Coarse lifecycle IPC (M2): emit a machine-readable status line on stdout at
    // key transitions so the spawning Ayther Play launcher can track this session.
    // Protocol = line-delimited "AYTHER_STATUS <json>"; all other stdout is human
    // logs the launcher forwards verbatim. (serde-side parsing arrives with the UI.)
    // #596: el `ready` lleva el manifest de la sesion. Es el hilo entre el
    // proceso vivo y su `launch.toml`: sin el, Play tiene un archivo en el disco
    // y un proceso corriendo sin manera de saber que son la misma partida — que
    // es justo lo que hace falta para detectar sesiones huerfanas (#601).
    std::fprintf(stdout,
                 "AYTHER_STATUS {\"event\":\"ready\",\"game_id\":\"%s\","
                 "\"has_pack\":%d,\"manifest\":\"%s\"}\n",
                 sess->game_id(), static_cast<int>(sess->has_pack()),
                 manifest_path.c_str());
    // #600: «que se esta jugando», para el overlay de Play. Va aparte del
    // `ready` porque responden preguntas distintas: `ready` dice que la sesion
    // arranco —el overlay se cierra— y este dice QUE arranco, que es lo que se
    // muestra. Juntarlos obligaria a Play a mirar el mismo evento dos veces con
    // dos intenciones.
    std::fprintf(stdout,
                 "AYTHER_STATUS {\"event\":\"now-playing\",\"game_id\":\"%s\","
                 "\"title\":\"%s\"}\n",
                 sess->game_id(), sess->game_id());

    // #600: un AVISO no corta la sesion. Un pack cargado con todos los
    // subsistemas apagados anda igual —se ve el original— y tratarlo como error
    // cerraria una partida que estaba por empezar bien. Es ademas el estado
    // NORMAL del modo Original (#598), asi que avisar es lo unico correcto:
    // decirlo, y seguir.
    if (sess->has_pack() && sess->subsystems_enabled_mask() == 0)
        std::fprintf(stdout,
                     "AYTHER_STATUS {\"event\":\"warning\",\"reason\":"
                     "\"el pack esta cargado pero ningun subsistema esta activo: "
                     "se ve el juego original\"}\n");

    if (crash_test) {
        std::fprintf(stdout, "AYTHER_STATUS {\"event\":\"crash-test\"}\n");
        std::fflush(stdout);
        std::abort();   // prove the launcher survives an abnormal child exit
    }

    // -----------------------------------------------------------------------
    // Pack hotreload watcher  (v0.9.5) — frontend policy: watch the .ay file
    // and ask the session to reload. Debounces 100 ms to ride out multi-write
    // "atomic" saves (write temp → rename into place).
    // -----------------------------------------------------------------------
    std::optional<ayther::engine::PackWatcher> pack_watcher;
    if (!pack_path.empty()) {
        auto watcher = ayther::engine::PackWatcher::create(pack_path);
        if (watcher) {
            pack_watcher.emplace(std::move(*watcher));
        }
    }
    Uint64 reload_due_ms = 0;   // non-zero while debounce is ticking
    if (pack_watcher)
        std::fprintf(stdout, "[main] Pack watcher active: %s\n", pack_path.c_str());

    // -----------------------------------------------------------------------
    // AytherRenderer — the motor's HD visual layer (R3.1). Owns the emu texture,
    // the HD tile cache, and the offscreen target; main.cpp blits its result to
    // the swapchain. Canvas = swapchain extent (1:1 final blit).
    // (CRT post-process is bypassed in R3.1; it returns inside the renderer in R3.2.)
    // -----------------------------------------------------------------------
    // The offscreen canvas is a 4:3 box that fits the window (not the raw window
    // size) so the Genesis 4:3 frame renders without horizontal stretch; the
    // present/CRT pillarbox it into the window with black bars.
    ayther::AytherRenderer renderer;
    bool renderer_ok = false;
    if (swap_ok) {
        const FitRect canvas_fit = aspect_fit(
            4, 3, swapchain.extent().width, swapchain.extent().height);
        renderer_ok = renderer.init(
            vulkan.engine_view(), static_cast<std::uint32_t>(canvas_fit.w),
            static_cast<std::uint32_t>(canvas_fit.h), AYTHER_SHADER_DIR);
        if (!renderer_ok)
            std::fprintf(stderr, "[main] AytherRenderer::init failed — no HD render.\n");
        // #140: activar el tier del pack para la ALTURA del canvas ANTES de la
        // primera carga de texturas (los caches indexan por nombre — cambiar el
        // tier después no las recargaría). Packs legacy: no-op.
        if (const auto pack = sess->pack()) {
            pack.select_render_tier_for_height(
                static_cast<std::uint32_t>(canvas_fit.h));
            if (const auto tiers = pack.render_tiers(); !tiers.is_legacy())
                std::fprintf(stdout,
                             "[main] Pack tiers 0x%x — canvas %dpx\n",
                             tiers.bits(), static_cast<int>(canvas_fit.h));
        }
    }
    // -----------------------------------------------------------------------
    // #561: pack overlays — this runtime is the Play frontend.
    //
    // The session reads and exposes them through pack_overlays(); the frontend
    // that renders them owns assembly of the layer stack (#390).
    //
    // `active_layer_stack` remains null when a pack has no overlays so the
    // renderer follows its default no-overlay path.
    // -----------------------------------------------------------------------
    AytherLayerStack pack_layer_stack;
    const AytherLayerStack* active_layer_stack = nullptr;
    const auto rebuild_pack_layer_stack = [&]() {
        pack_layer_stack = AytherLayerStack{};
        active_layer_stack = nullptr;
        if (!sess->has_pack()) {
            return;
        }
        const std::size_t appended_overlay_count =
            ayther_runtime::build_pack_overlay_stack(
                sess->pack_overlays(), pack_layer_stack);
        if (appended_overlay_count != 0U) {
            active_layer_stack = &pack_layer_stack;
            std::fprintf(stdout, "[main] Pack overlays: %zu layers\n",
                         appended_overlay_count);
        }
    };
    rebuild_pack_layer_stack();

    // (The HD sprite overlay now lives inside the renderer — it renders into the
    //  offscreen target alongside the emu frame + tiles.)

    // -----------------------------------------------------------------------
    // VkPostProcess — CRT presentation pass (frontend, R3.2). Samples the
    // renderer's offscreen HD frame and renders it to the swapchain with CRT
    // scanlines + vignette. Optional: if the SPIR-V is missing, the present path
    // falls back to a plain blit. Re-bound to the offscreen view on init + resize
    // (kept in the frontend so the Lab can show the clean frame in R3.3).
    // -----------------------------------------------------------------------
    VkPostProcess postprocess;
    bool postprocess_ok = false;
    if (swap_ok && renderer_ok) {
        postprocess_ok = postprocess.init(vulkan, swapchain,
                                          AYTHER_SHADER_DIR "postprocess.vert.spv",
                                          AYTHER_SHADER_DIR "postprocess.frag.spv");
        if (postprocess_ok)
            postprocess.set_source(vulkan, renderer.render_image());
        else
            std::fprintf(stdout, "[main] VkPostProcess disabled — plain blit fallback.\n");
    }

    // -----------------------------------------------------------------------
    // Main loop
    // -----------------------------------------------------------------------
    Uint64   last_print_ms = SDL_GetTicks();   // frame index now comes from fv.frame_index

    // ---------------------------------------------------------------------------
    // Frame pacing — nanosecond precision via SDL_GetTicksNS / SDL_DelayNS.
    //
    // Frame duration is derived from retro_system_av_info.timing.fps so the
    // engine runs at the core's exact hardware rate:
    //   NTSC Mega Drive  → 59.9227 Hz  → 16,688,754 ns/frame
    //   PAL  Mega Drive  → 50.0000 Hz  → 20,000,000 ns/frame
    //
    // The old kFrameMs=16 ceiling (~62.5 Hz) ran faster than NTSC and caused
    // visible audio/video drift over time.  Clamped [20, 120] Hz to guard
    // against corrupt core metadata.
    // ---------------------------------------------------------------------------
    const double core_fps    = sess->timing_fps();   // from retro_system_av_info
    const double fps_clamped = (core_fps < 20.0) ? 20.0
                             : (core_fps > 120.0) ? 120.0
                             : core_fps;
    const Uint64 frame_ns    = static_cast<Uint64>(1'000'000'000.0 / fps_clamped);
    std::fprintf(stdout, "[main] Frame pacing: %.4f Hz  →  %" PRIu64 " ns/frame\n",
                 fps_clamped, frame_ns);
    Uint64 next_frame_ns = SDL_GetTicksNS() + frame_ns;

    // Input source (M3): SDL keyboard + gamepad → libretro RetroPad bitfield,
    // fed into the motor each frame via AytherSession::set_input.
    ayther::GameInput input;

    // In-game pause overlay + HD↔Original toggle (M4).
    ayther::PlayerOverlay overlay;
    if (swap_ok) {
        if (!overlay.init(vulkan, swapchain, window))
            std::fprintf(stderr, "[main] PlayerOverlay::init failed — overlay disabled.\n");
    }
    bool hd_on_ = true;   // HD substitutions on by default; toggled by overlay or F1
    bool shaders_on_ = true;   // #299: post-proceso de presentación (CRT)
    // #296: el perfil de SALIDA activo. Puntero a la tabla estatica del
    // Runtime: cambiar de perfil es mover este puntero, y por eso no hay que
    // reiniciar nada.
    const ayther::runtime::OutputProfile* out_profile_ =
        &ayther::runtime::output_profile_default();
    // #298: comparación con pantalla dividida. Apagada por default — cuesta un
    // segundo render por frame y no es el modo de jugar.
    bool  capture_req_    = false;   // #300: F12 pidio una captura
    bool  diagnose_req_   = false;   // #302: F9 pidio el diagnostico
    // #302: el ultimo frame REAL medido. No un benchmark aparte: una
    // prueba sintetica mediria otra cosa que la que el jugador esta viendo.
    float last_frame_ms_ = 0.0f;
    bool  split_on_       = false;
    bool  split_vertical_ = false;   // false = divisor vertical (izq/der)
    float split_pos_      = 0.5f;

    // #299: la configuración del jugador para ESTA combinación de juego y pack.
    //
    // Se carga antes del primer frame y se aplica sobre la sesión: si no se
    // aplicara acá, el panel mostraría lo guardado y el juego se vería con otra
    // cosa hasta que alguien abriera el menú.
    const std::filesystem::path& cfg_dir =
        runtime_config.paths().configuration_directory();
    std::string cfg_pack_name;
    if (const auto pack = sess->pack()) {
        cfg_pack_name = pack.info().name;
    }
    const std::filesystem::path cfg_file =
        ayther::player_config_path(cfg_dir, sess->game_id(), cfg_pack_name);
    ayther::PlayerConfig player_cfg = ayther::player_config_load(cfg_file);
    {
        // Orden: primero el PERFIL y después la máscara guardada. El perfil
        // pisa la máscara, así que aplicarlo después borraría los ajustes
        // sueltos que el jugador dejó — y ésos son más específicos.
        if (!player_cfg.profile.empty()) sess->set_profile(player_cfg.profile);
        if (player_cfg.have_subsystems) {
            // Se INTERSECTA con lo que este pack trae: la configuración puede
            // venir de una versión anterior del pack que traía más cosas, y
            // encender un subsistema ausente no lo hace aparecer — sólo dejaría
            // el panel mostrando algo que no pasa.
            uint32_t avail = 0;
            for (uint32_t i = 0; i < ayther::kSubsystemCount; ++i)
                if (sess->subsystem_availability(static_cast<ayther::Subsystem>(i))
                    != ayther::SubsystemAvailability::Absent)
                    avail |= (1u << i);
            sess->set_subsystems_enabled_mask(player_cfg.subsystems & avail);
        }
        for (uint32_t i = 0; i < ayther::kAudioBusCount; ++i) {
            sess->set_bus_volume(static_cast<ayther::AudioBus>(i), player_cfg.bus_gain[i]);
            sess->set_bus_muted (static_cast<ayther::AudioBus>(i), player_cfg.bus_muted[i]);
        }
        shaders_on_ = player_cfg.shaders_on;
        hd_on_      = player_cfg.hd_on;
        // #311: lo que Play pidio pisa lo guardado. Es mas especifico —el
        // jugador acaba de elegirlo— y ademas es la unica forma de que la
        // pantalla previa signifique algo cuando ya hay config guardada.
        if (have_subsystems_arg)
            sess->set_subsystems_enabled_mask(static_cast<uint32_t>(subsystems_arg));
        if (have_mute_arg)
            for (uint32_t i = 0; i < ayther::kAudioBusCount; ++i)
                sess->set_bus_muted(static_cast<ayther::AudioBus>(i),
                                    (mute_buses_arg & (1 << i)) != 0);
        if (shaders_arg >= 0) shaders_on_ = shaders_arg != 0;
        // #296: el perfil de SALIDA. La precedencia la decide
        // `output_profile_resolve` y no este bloque: lo que el usuario
        // eligio gana sobre lo que el pack recomienda, y si no ganara,
        // «recomendar» seria imponer con otro nombre.
        std::string pack_output;
        if (const auto pack = sess->pack()) {
            pack_output = pack.info().recommended_output_profile;
        }
        const std::string user_output =
            !output_arg.empty() ? output_arg : player_cfg.output;
        out_profile_ = &ayther::runtime::output_profile_resolve(
            user_output, pack_output);
        player_cfg.output = std::string(out_profile_->id);
    }

    // Last valid FrameView — kept alive across paused frames (step() not called
    // while paused, so the previous frame's pointers remain valid).
    static const ayther::FrameView kEmptyFrame{};
    const ayther::FrameView* last_fv = nullptr;

    // #604 - reanudar, si Play dijo desde donde.
    //
    // Va DESPUES de aplicar la configuracion y ANTES del primer frame: cargado
    // antes, la config lo pisaria; cargado despues, el jugador veria un frame
    // de la pantalla de titulo antes del salto, que es exactamente el «arranco
    // de cero» que venia a evitar.
    //
    // Si falla, se juega igual desde el principio Y SE AVISA. Callarlo dejaria
    // a alguien creyendo que perdio la partida cuando el archivo sigue ahi; y
    // negarse a arrancar convertiria un guardado ilegible en un juego que no se
    // puede jugar.
    if (!load_state_arg.empty()) {
        std::vector<std::uint8_t> loaded_state;
        bool state_read_successfully = false;
        if (std::FILE* state_file =
                std::fopen(load_state_arg.c_str(), "rb")) {
            std::fseek(state_file, 0, SEEK_END);
            const long state_size = std::ftell(state_file);
            std::fseek(state_file, 0, SEEK_SET);
            if (state_size > 0) {
                loaded_state.resize(static_cast<std::size_t>(state_size));
                state_read_successfully =
                    std::fread(loaded_state.data(), 1, loaded_state.size(),
                               state_file) == loaded_state.size();
            }
            std::fclose(state_file);
        }
        const bool state_restored =
            state_read_successfully && sess && sess->unserialize(loaded_state);
        if (state_restored) {
            std::fprintf(stdout, "[main] reanudado desde %s (%zu bytes)\n",
                         load_state_arg.c_str(), loaded_state.size());
        } else {
            std::fprintf(stdout,
                         "AYTHER_STATUS {\"event\":\"warning\",\"reason\":"
                         "\"no se pudo reanudar desde el guardado: se empieza de "
                         "cero y el guardado sigue donde estaba\"}\n");
        }
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // M4: overlay consumes the event first (ImGui keyboard/gamepad nav).
            overlay.handle_event(event);
            input.handle_event(event);   // gamepad hotplug

            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN) {
                // Escape — toggle pause (both while playing and to resume).
                if (event.key.scancode == SDL_SCANCODE_ESCAPE)
                    overlay.toggle_pause();
                // F1 — toggle HD mode instantly (without opening the menu).
                if (event.key.scancode == SDL_SCANCODE_F1) {
                    hd_on_ = !hd_on_;
                    std::fprintf(stdout, "[main] HD mode: %s\n", hd_on_ ? "ON" : "OFF");
                }
                // #298: F2 enciende y apaga la comparacion dividida; F3 cambia
                // la orientacion. Se ocultan SIN reiniciar --es un criterio de
                // la issue-- porque el modo es solo un `if` en el present.
                if (event.key.scancode == SDL_SCANCODE_F2) {
                    split_on_ = !split_on_;
                    std::fprintf(stdout, "[main] split: %s\n", split_on_ ? "ON" : "OFF");
                }
                if (event.key.scancode == SDL_SCANCODE_F12) capture_req_ = true;
                if (event.key.scancode == SDL_SCANCODE_F9) diagnose_req_ = true;
                // #296: F4 cicla el perfil de SALIDA. Sin reiniciar nada —
                // cambiar de perfil es mover un puntero a la tabla estatica.
                if (event.key.scancode == SDL_SCANCODE_F4) {
                    const auto all = ayther::runtime::output_profiles();
                    std::size_t cur = 0;
                    for (std::size_t i = 0; i < all.size(); ++i)
                        if (&all[i] == out_profile_) cur = i;
                    out_profile_ = &all[(cur + 1) % all.size()];
                    // Ciclar es ELEGIR: a partir de aca manda el usuario y
                    // no la recomendacion del pack.
                    player_cfg.output = std::string(out_profile_->id);
                    ayther::player_config_save(cfg_file, player_cfg);
                    std::fprintf(stdout, "[main] salida: %.*s\n",
                                 static_cast<int>(out_profile_->name.size()),
                                 out_profile_->name.data());
                }
                if (event.key.scancode == SDL_SCANCODE_F3 && split_on_)
                    split_vertical_ = !split_vertical_;
                // Mover el divisor con el teclado en pasos, ademas del arrastre
                // con el mouse: en un split lo que se compara suele ser un
                // detalle, y arrastrar con precision de un pixel es peor que
                // empujarlo de a poco.
                if (split_on_ && event.key.scancode == SDL_SCANCODE_LEFT)
                    split_pos_ = split_pos_ > 0.02f ? split_pos_ - 0.02f : 0.0f;
                if (split_on_ && event.key.scancode == SDL_SCANCODE_RIGHT)
                    split_pos_ = split_pos_ < 0.98f ? split_pos_ + 0.02f : 1.0f;
            } else if (event.type == SDL_EVENT_MOUSE_MOTION && split_on_) {
                // Arrastre: solo con el boton apretado. Mover el divisor con el
                // mouse suelto haria imposible mirar una zona sin que se corra.
                if (event.motion.state & SDL_BUTTON_LMASK) {
                    int w = 1, h = 1;
                    SDL_GetWindowSize(window, &w, &h);
                    const float t = split_vertical_
                        ? (h ? event.motion.y / static_cast<float>(h) : 0.5f)
                        : (w ? event.motion.x / static_cast<float>(w) : 0.5f);
                    split_pos_ = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
                }
            } else if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
                // Guide / Home button → pause (system-level, not a game button).
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE)
                    overlay.toggle_pause();
                // #298 con GAMEPAD: el criterio de la issue pide moverlo con
                // mouse O gamepad. Los hombros mueven el divisor y el stick
                // click lo enciende -- sin robarle botones al juego, que es lo
                // que pasaria usando la cruceta o los de accion.
                if (event.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK)
                    split_on_ = !split_on_;
                if (split_on_ && event.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
                    split_pos_ = split_pos_ > 0.05f ? split_pos_ - 0.05f : 0.0f;
                if (split_on_ && event.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
                    split_pos_ = split_pos_ < 0.95f ? split_pos_ + 0.05f : 1.0f;
            } else if (event.type == SDL_EVENT_WINDOW_RESIZED && swap_ok) {
                // Only rebuild when it is the *main* game window that resized.
                const SDL_WindowID game_wid = SDL_GetWindowID(window);
                if (event.window.windowID == game_wid) {
                    int w = event.window.data1;
                    int h = event.window.data2;
                    swap_ok = swapchain.rebuild(vulkan,
                                               static_cast<uint32_t>(w),
                                               static_cast<uint32_t>(h));
                    if (swap_ok && renderer_ok) {
                        const FitRect cf = aspect_fit(4, 3, swapchain.extent().width,
                                                            swapchain.extent().height);
                        renderer.resize(
                            vulkan.engine_view(), static_cast<uint32_t>(cf.w),
                            static_cast<uint32_t>(cf.h));
                        if (postprocess_ok) {   // re-bind to the new offscreen view
                            postprocess.rebuild(vulkan, swapchain);
                            postprocess.set_source(vulkan, renderer.render_image());
                        }
                        overlay.rebuild(vulkan, swapchain);   // M4: rebuild overlay FBs
                    }
                }
            }
        }

        // ---- Pack hotreload (v0.9.5) ----------------------------------------
        // poll() drains the OS event queue; set a 100 ms debounce timer on
        // the first change so multi-write saves don't trigger double reloads.
        if (pack_watcher && pack_watcher->poll()) {
            if (reload_due_ms == 0)
                reload_due_ms = SDL_GetTicks() + 100;
        }
        if (reload_due_ms != 0 && SDL_GetTicks() >= reload_due_ms) {
            reload_due_ms = 0;
            std::fprintf(stdout, "[main] Pack change detected — hotreloading %s\n",
                         pack_path.c_str());

            // GPU must be idle before freeing textures that may be in flight.
            if (vulkan.is_ready())
                vkDeviceWaitIdle(vulkan.device());

            // Evict GPU texture caches so next frame re-fetches from new pack
            // (the renderer evicts both its tile cache and the sprite textures).
            renderer.evict_pack_textures(vulkan.engine_view());

            // The session re-opens the pack from disk and rewires the script +
            // substitutors (and reloads scripts/init.lua). The frontend only had
            // to evict its GPU caches above.
            if (auto rr = sess->reload_pack(); rr) {
                if (sess->has_pack()) {
                    // #140: el reload re-abre el pack (tier default = el más
                    // alto) → re-activar el tier del canvas vigente.
                    if (const auto pack = sess->pack(); pack && renderer_ok) {
                        pack.select_render_tier_for_height(
                            renderer.render_image().extent.height);
                    }
                    std::fprintf(stdout, "[main] Pack reloaded  game=%s\n", sess->game_id());
                } else {
                    std::fprintf(stdout, "[main] Pack gone — continuing without assets\n");
                }
                // #561: el stack de Acetatos es del pack — se rearma con él
                // (y se apaga si el pack se fue). Las texturas ya se
                // evictaron arriba, así que las láminas nuevas se re-fetchean.
                rebuild_pack_layer_stack();
            } else {
                std::fprintf(stderr, "[main] Pack reload failed: %s\n",
                             rr.error.message.c_str());
            }
        }
        // ---------------------------------------------------------------------

        // Advance the emulation. While paused, step() is frozen so the last
        // FrameView remains valid (its pointers live until the next step()).
        //   Backspace → rewind   (R6, walk back through the savestate ring)
        //   Tab held  → fast-forward (R6, run extra frames per displayed frame)
        // M4: skip stepping while the pause overlay is open.
        if (!overlay.is_paused()) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const bool rewinding = keys && keys[SDL_SCANCODE_BACKSPACE];
            const bool ffwd      = keys && keys[SDL_SCANCODE_TAB];

            // El modo HD también gatea el matcher de sets de plano (Utilería y
            // Glifos del pack): suprime los tiles originales, así que en modo
            // Original tiene que estar apagado o quedan agujeros. Se hace acá,
            // ANTES del step, porque la supresión ocurre en produce_frame —
            // pasarlo sólo a renderer.render() no alcanza.
            sess->set_hd_enabled(hd_on_);
            // …y el AUDIO con él (#297). La sustitución en vivo se encendía una
            // sola vez al cargar el pack y no se apagaba nunca, así que poner
            // «HD Mode: OFF» dejaba la imagen original con la música HD encima
            // — el toggle decía una cosa y sonaba otra. El alcance de la issue
            // pide justamente que video y audio cambien SINCRONIZADOS.
            // Sólo si el pack trae eventos: sin asignaciones no hay nada que
            // sustituir y encenderlo sería levantar el detector al pedo.
            if (sess->audio_event_assignment_count() > 0)
                sess->set_audio_runtime_substitution(hd_on_);

            if (rewinding) {
                if (const ayther::FrameView* rv = sess->rewind_step())
                    last_fv = rv;            // walked back one frame
                // else: buffer exhausted — hold on the oldest frame
            } else {
                sess->set_input(0, input.poll());
                last_fv = &sess->step();
                if (ffwd) {
                    // Run a few extra frames this display tick (capped) — each
                    // still feeds the rewind ring + audio. ~4× speed.
                    for (int i = 0; i < 3; ++i) { sess->set_input(0, input.poll()); last_fv = &sess->step(); }
                }
            }
        }
        const ayther::FrameView& fv = last_fv ? *last_fv : kEmptyFrame;

        // --frames N (dev/CI): clean exit once N frames have been emulated, so the
        // launcher slice + headless tests get a deterministic, self-terminating run.
        // #502: captura programada (mismo camino que F12) — el e2e del pack sin
        // teclado: SDL no recibe teclas sintéticas.
        for (int cf : capture_at)
            if (cf > 0 && fv.frame_index == static_cast<uint64_t>(cf)) capture_req_ = true;
        if (frames_limit > 0 && fv.frame_index >= static_cast<uint64_t>(frames_limit)) {
            std::fprintf(stdout, "[main] --frames %d reached; exiting cleanly.\n", frames_limit);
            running = false;   // the authoritative exit status is emitted post-loop
        }

        // ----------------------------------------------------------------
        // Sonic RAM reads (debug overlay)
        // ----------------------------------------------------------------
        const auto telemetry = ayther::runtime::decode_sonic_telemetry(
            std::span<const std::uint8_t>{sess->work_ram(),
                                          sess->work_ram_size()});
        const auto px = telemetry.x;
        const auto py = telemetry.y;
        const auto vx = telemetry.velocity_x;
        const auto vy = telemetry.velocity_y;
        const bool xy_ok = telemetry.has_position;
        const bool vel_ok = telemetry.has_velocity;

        // ---- Vulkan present path ------------------------------------------
        if (swap_ok && renderer_ok && fv.fb_pixels) {
            if (swapchain.begin_frame(vulkan)) {
                const auto      pack = sess->pack();   // typed borrowed HD assets
                VkCommandBuffer cmd  = swapchain.current_frame().cmd;

                // The motor renders the emulator frame + HD tile/sprite subs
                // into its offscreen image.  hd_on_ = false → emu frame only
                // (Original mode, M4).
                // #298: comparación con pantalla dividida.
                //
                // El MISMO FrameView se renderiza dos veces —original y HD— en
                // el mismo comando, y la copia intermedia vive en la imagen de
                // comparación del renderer (#305). Por eso las dos mitades no
                // se pueden desincronizar: no existe el estado intermedio donde
                // una avance sin la otra.
                //
                // Cuesta un segundo render por frame, y por eso sólo se paga
                // cuando el modo está encendido.
                bool split_ready = false;
                if (split_on_ && hd_on_) {
                    renderer.render(vulkan.engine_view(), cmd, fv, pack,
                                    /*hd=*/false, active_layer_stack);
                    split_ready = renderer.capture_compare(
                        vulkan.engine_view(), cmd);
                }
                renderer.render(vulkan.engine_view(), cmd, fv, pack, hd_on_,
                                active_layer_stack);

                // Present the offscreen HD frame: CRT post-process (samples it)
                // when the shaders are present, else a plain blit. Both leave the
                // swapchain in TRANSFER_DST for finalize().
                if (split_ready) {
                    // El post-proceso queda fuera del split a propósito: mezcla
                    // las dos mitades en un solo pase y compararlas con el CRT
                    // encima compararía el shader, no la remasterización.
                    VkPresent::blit_split_to_swapchain(
                        vulkan, swapchain,
                        renderer.compare_render_image(), renderer.render_image(),
                        split_pos_, split_vertical_);
                } else if (postprocess_ok) {
                    const AytherShaderParams& sp = fv.shader_params;
                    const float time_s = static_cast<float>(SDL_GetTicks()) * 0.001f;
                    // #296: el perfil ESCALA lo que el pack pidio y pone lo
                    // suyo cuando el pack no pidio nada. Las dos mitades de esa
                    // regla viven en `output_shader`, que se prueba sin GPU.
                    const ayther::runtime::OutputShader fx =
                        shaders_on_
                            ? ayther::runtime::output_shader(
                                  *out_profile_, sp.crt_strength,
                                  sp.scan_strength, sp.vignette)
                            : ayther::runtime::OutputShader{
                                  0.0f, 0.0f, 0.0f, 0.0f };

                    // Y el rect: el ESCALADO tambien es del perfil.
                    //
                    // La fuente que se le pasa NO es la misma en los dos modos,
                    // y no es un descuido:
                    //
                    //   · Fit sólo mira el ASPECTO, y el del canvas offscreen es
                    //     4:3 exacto. Pasarle el nativo (320x224 = 10:7) movería
                    //     la imagen dos píxeles y estiraría lo que hoy calza.
                    //   · Integer mira el TAMAÑO: el múltiplo entero es del alto
                    //     NATIVO —224 o 240 líneas—, que es lo que hace que cada
                    //     fila del juego ocupe las mismas filas de pantalla. Con
                    //     el canvas HD no entraría ni ×1 y «Pixel-perfect»
                    //     caería siempre a fit, que es no hacer nada.
                    const uint32_t emu_h_px = fv.fb_height ? fv.fb_height : kEmuH;
                    const bool use_integer_scaling =
                        out_profile_->scaling
                            == ayther::runtime::OutputScaling::Integer;
                    const ayther::runtime::OutputRect r =
                        use_integer_scaling
                            ? ayther::runtime::output_rect(
                                  *out_profile_, (emu_h_px * 4 + 1) / 3,
                                  emu_h_px, swapchain.extent().width,
                                  swapchain.extent().height)
                            : ayther::runtime::output_rect(
                                  *out_profile_, 4, 3,
                                  swapchain.extent().width,
                                  swapchain.extent().height);
                    postprocess.apply(vulkan, swapchain,
                        static_cast<float>(swapchain.extent().width),
                        static_cast<float>(swapchain.extent().height),
                        static_cast<float>(emu_h_px),
                        time_s,
                        OutDestRect{ r.x, r.y, r.w, r.h },
                        out_profile_->smoothing,
                        fx.crt, fx.scan, fx.vignette,
                        // #230 EM-7.2: el sangrado NTSC es ABSOLUTO del perfil
                        // y no un escalado de lo que pide el pack — el autor no
                        // autora un `ntsc`, porque el sangrado no es una
                        // decision sobre su arte sino sobre que televisor imita
                        // quien mira. Escalarlo contra un valor que nadie
                        // autora daria siempre cero.
                        fx.ntsc);
                } else {
                    VkPresent::blit_to_swapchain(
                        vulkan, swapchain,
                        renderer.render_image(),
                        out_profile_->scaling
                            == ayther::runtime::OutputScaling::Integer,
                        out_profile_->smoothing);
                }

                // #300: captura comparativa sincronizada.
                //
                // El «mismo frame lógico» no se consigue coordinando dos
                // capturas: se consigue no teniendo dos. `export_frame` NO
                // avanza la emulación —renderiza el `fv` que ya está— así que
                // las tres imágenes salen del mismo cuadro por construcción.
                //
                // Se hace fuera del camino de present y una sola vez: cuesta
                // dos renders y dos esperas de fence, y eso a 60 fps se nota.
                // #302: el diagnóstico. Mide lo que se puede medir sin
                // hardware de referencia y SUGIERE un perfil de salida — no lo
                // aplica: un asistente que cambia lo que midió convierte una
                // recomendación en una imposición.
                //
                // El informe se escribe en un archivo LOCAL. No hay red en este
                // camino, así que «no recopila información sin consentimiento»
                // es una propiedad del código y no una promesa.
                if (diagnose_req_) {
                    diagnose_req_ = false;
                    ayther::DiagnosticReport dr;
                    dr.vulkan = has_vulkan;
                    if (const SDL_DisplayMode* dm =
                            SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay())) {
                        dr.display_w  = static_cast<uint32_t>(dm->w);
                        dr.display_h  = static_cast<uint32_t>(dm->h);
                        dr.refresh_hz = dm->refresh_rate;
                    }
                    // La escala entera se mide contra la VENTANA y no contra el
                    // escritorio: es donde el juego se va a ver, y en ventana
                    // los números son otros.
                    {
                        int ww = 0, wh = 0;
                        SDL_GetWindowSize(window, &ww, &wh);
                        if (ww > 0 && wh > 0) {
                            const uint32_t kx = static_cast<uint32_t>(ww) / kEmuW;
                            const uint32_t ky = static_cast<uint32_t>(wh) / kEmuH;
                            dr.integer_scale = kx < ky ? kx : ky;
                        }
                    }
                    // El frame medido es el ÚLTIMO real, no un benchmark
                    // aparte: correr una prueba sintética mediría otra cosa que
                    // la que el jugador está viendo.
                    dr.frame_ms = last_frame_ms_;
                    {
                        SDL_AudioSpec spec{};
                        int frames = 0;
                        if (SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                     &spec, &frames)) {
                            dr.audio_freq     = spec.freq;
                            dr.audio_channels = spec.channels;
                            if (spec.freq > 0)
                                dr.buffer_latency_ms =
                                    1000.0f * static_cast<float>(frames) / spec.freq;
                        }
                    }
                    if (has_vulkan) {
                        VkPhysicalDeviceProperties props{};
                        vkGetPhysicalDeviceProperties(vulkan.physical_device(), &props);
                        dr.gpu_name = props.deviceName;
                    }
                    ayther::diagnose_suggest(dr);

                    const std::filesystem::path dp =
                        runtime_config.paths().diagnostics_file();
                    std::error_code dec;
                    std::filesystem::create_directories(dp.parent_path(), dec);
                    std::ofstream df(dp, std::ios::binary | std::ios::trunc);
                    const std::string md = ayther::diagnostic_report_markdown(dr);
                    if (df) df.write(md.data(),
                                     static_cast<std::streamsize>(md.size()));
                    std::fprintf(stdout,
                        "[main] diagnostico: sugiere '%s' — %s\n           informe: %s\n",
                        dr.suggested.c_str(), dr.reason.c_str(),
                        dp.string().c_str());
                }

                if (capture_req_) {
                    capture_req_ = false;
                    if (renderer.readback_init(vulkan.engine_view())) {
                        const VkExtent2D ex = renderer.render_image().extent;
                        std::vector<uint8_t> orig;
                        if (const uint8_t* p0 =
                                renderer.export_frame(
                                    vulkan.engine_view(), fv, pack,
                                    /*hd=*/false, active_layer_stack))
                            orig.assign(p0, p0 + static_cast<size_t>(ex.width) * ex.height * 4);
                        const uint8_t* p1 =
                            renderer.export_frame(
                                vulkan.engine_view(), fv, pack,
                                /*hd=*/true, active_layer_stack);
                        if (!orig.empty() && p1) {
                            ayther::CaptureMeta m;
                            m.game_id  = sess->game_id();
                            m.pack_name = cfg_pack_name;
                            if (const auto current_pack = sess->pack()) {
                                m.pack_build = current_pack.info().build_id;
                            }
                            m.profile = sess->active_profile();
                            char ts[32];
                            const std::time_t now = std::time(nullptr);
                            std::strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%S",
                                          std::gmtime(&now));
                            m.timestamp = ts;
                            m.width  = ex.width;
                            m.height = ex.height;
                            std::string base = "captura-";
                            base += ts;
                            for (char& c : base) if (c == ':') c = '-';
                            const std::string out = ayther::capture_write(
                                runtime_config.paths().captures_directory(),
                                base, orig.data(), p1,
                                ex.width, ex.height, split_pos_, split_vertical_, m);
                            std::fprintf(stdout, "[main] captura: %s\n",
                                         out.empty() ? "FALLO" : out.c_str());
                        }
                        renderer.readback_shutdown(vulkan.engine_view());
                    }
                }

                // M4: pause overlay — draws the menu over the swapchain image
                // (after CRT/blit, before finalize). No-op when not paused.
                overlay.render(vulkan, cmd, swapchain, hd_on_, running,
                               sess.get(), &player_cfg, &shaders_on_);
                // #299: guardar al SALIR del panel, no en cada movimiento de un
                // slider — eso escribiría el archivo sesenta veces por segundo.
                if (overlay.take_config_dirty() && !overlay.is_paused()) {
                    player_cfg.hd_on = hd_on_;
                    ayther::player_config_save(cfg_file, player_cfg);
                }

                // Transition swap → PRESENT_SRC_KHR.
                VkPresent::finalize(vulkan, swapchain);

                if (!swapchain.end_frame(vulkan)) {
                    // Swapchain out of date — rebuild it + re-track the offscreen
                    // canvas to the new size (skip 0×0 / minimised).
                    int w, h;
                    SDL_GetWindowSizeInPixels(window, &w, &h);
                    swap_ok = swapchain.rebuild(vulkan,
                                               static_cast<uint32_t>(w),
                                               static_cast<uint32_t>(h));
                    if (swap_ok && renderer_ok && w > 0 && h > 0) {
                        const FitRect cf = aspect_fit(4, 3, swapchain.extent().width,
                                                            swapchain.extent().height);
                        renderer.resize(
                            vulkan.engine_view(), static_cast<uint32_t>(cf.w),
                            static_cast<uint32_t>(cf.h));
                        if (postprocess_ok) {
                            postprocess.rebuild(vulkan, swapchain);
                            postprocess.set_source(vulkan, renderer.render_image());
                        }
                    }
                }
            }
        }

        Uint64 now_ms = SDL_GetTicks();
        // #302: el costo del frame que se acaba de presentar. Promediado
        // suavemente para que el diagnostico no dependa de un pico suelto —
        // una carga de asset o un cambio de escena moverian el numero y la
        // sugerencia saldria distinta segun cuando se apreto la tecla.
        {
            static Uint64 prev_ms = 0;
            if (prev_ms) {
                const float dt = static_cast<float>(now_ms - prev_ms);
                last_frame_ms_ = last_frame_ms_ > 0.0f
                    ? last_frame_ms_ * 0.9f + dt * 0.1f : dt;
            }
            prev_ms = now_ms;
        }
        if (now_ms - last_print_ms >= 1000) {
            std::fprintf(stdout,
                "[frame %5" PRIu64 "]"
                "  XY=(%5d,%5d)"
                "  vel=(%4d,%4d)"
                "  tiles=%u  sprites=%u  audio=%u"
                "  vk=%s\n",
                fv.frame_index,
                xy_ok  ? px : -1, xy_ok  ? py : -1,
                vel_ok ? vx :  0, vel_ok ? vy :  0,
                fv.unique_tile_count,
                fv.unique_sprite_count,
                fv.unique_audio_count,
                swap_ok ? vulkan.gpu_name().c_str() : "off"
            );
            last_print_ms = now_ms;
        }

        // (audio SFX reaping now happens inside AytherSession::step())

        {
            const Uint64 now_ns = SDL_GetTicksNS();
            if (now_ns < next_frame_ns)
                SDL_DelayNS(next_frame_ns - now_ns);
            else
                next_frame_ns = now_ns;  // fell behind — resync, don't chase missed frames
            next_frame_ns += frame_ns;
        }
    }

    // -----------------------------------------------------------------------
    // Shutdown
    // -----------------------------------------------------------------------

    // ---- Cloud save: serialize state before teardown (M7) -------------------
    // Saves the full emulator savestate to the platform data directory so the
    // Rust launcher can upload it to the Hub for cloud sync.  Best-effort:
    // if serialization fails, the game still exits cleanly (no data loss,
    // just no cloud upload this session).
    std::string savestate_path;
    {
        std::vector<uint8_t> state_data;
        if (sess && sess->serialize(state_data)) {
            // #603: el layout es <saves>/<game_id>/<perfil>/estado-<cuando>-<crc>.bin
            //
            // Antes era UN archivo por juego con nombre fijo, en el arbol del
            // Runtime. Eso dejaba tres agujeros: guardar pisaba la partida
            // anterior sin vuelta atras; el estado de una partida en HD y el de
            // la misma en original compartian archivo (y un savestate es una
            // foto de la memoria, asi que mezclarlos da un juego que se
            // comporta raro media hora despues); y Play, que guarda en OTRA
            // carpeta, no lo encontraba.
            //
            // La revision de la ROM va en el NOMBRE para que sobreviva a copiar
            // la carpeta, que es lo que la gente hace con sus guardados.
            const auto& saves_root = runtime_config.saves_directory();

            const auto sanitize_path_component = [](const std::string& value) {
                std::string sanitized;
                for (const char character : value) {
                    sanitized +=
                        (std::isalnum(
                             static_cast<unsigned char>(character)) ||
                         character == '-' || character == '_')
                            ? character
                            : '_';
                }
                return sanitized.empty() ? std::string("sin_nombre") : sanitized;
            };
            const std::string game_id = sess->game_id();
            const std::string game_component =
                game_id.empty() ? "unknown" : game_id;
            std::string active_profile = sess->active_profile();
            if (active_profile.empty()) {
                active_profile = "original";
            }

            const auto save_directory =
                saves_root / sanitize_path_component(game_component) /
                sanitize_path_component(active_profile);
            std::error_code directory_error;
            std::filesystem::create_directories(save_directory,
                                                directory_error);

            char timestamp[32];
            const std::time_t now = std::time(nullptr);
            std::strftime(timestamp, sizeof(timestamp), "%Y%m%dT%H%M%SZ",
                          std::gmtime(&now));
            std::string rom_revision =
                rom_crc32_arg.empty()
                    ? "sinrev"
                    : sanitize_path_component(rom_crc32_arg);
            for (char& character : rom_revision) {
                character = static_cast<char>(std::tolower(
                    static_cast<unsigned char>(character)));
            }

            const auto destination_path =
                save_directory /
                ("estado-" + std::string(timestamp) + "-" + rom_revision +
                 ".bin");
            const auto temporary_path = destination_path.string() + ".tmp";

            // ATOMICO: temporal y rename. Escribir encima del guardado bueno
            // significa que entre el primer byte y el ultimo no hay partida, y
            // ese es justo el momento en que la consola se apaga.
            bool save_succeeded = false;
            if (std::FILE* state_file =
                    std::fopen(temporary_path.c_str(), "wb")) {
                save_succeeded =
                    std::fwrite(state_data.data(), 1, state_data.size(),
                                state_file) == state_data.size();
                // El fflush ANTES del fclose: sin el, un fclose que falle deja
                // un temporal a medias que el rename convertiria en el
                // guardado bueno.
                if (save_succeeded) {
                    save_succeeded = std::fflush(state_file) == 0;
                }
                if (std::fclose(state_file) != 0) {
                    save_succeeded = false;
                }
            }
            if (save_succeeded) {
                std::error_code rename_error;
                std::filesystem::rename(temporary_path, destination_path,
                                        rename_error);
                save_succeeded = !rename_error;
                if (!save_succeeded) {
                    std::filesystem::remove(temporary_path, rename_error);
                }
            } else {
                std::error_code remove_error;
                std::filesystem::remove(temporary_path, remove_error);
            }

            if (save_succeeded) {
                savestate_path = destination_path.string();
                std::fprintf(stdout, "[main] guardado: %zu bytes -> %s\n",
                             state_data.size(), savestate_path.c_str());
            } else {
                std::fprintf(stderr, "[main] no se pudo escribir el guardado: %s\n",
                             destination_path.string().c_str());
            }
        }
    }

    // Coarse lifecycle IPC (M2 / M7): emit exit event with optional savestate
    // path so the launcher can upload the cloud save.
    if (savestate_path.empty()) {
        std::fprintf(stdout, "AYTHER_STATUS {\"event\":\"exit\"}\n");
    } else {
        // Escape backslashes in path for valid JSON (Windows paths).
        std::string escaped;
        for (char c : savestate_path) {
            escaped += (c == '\\') ? '/' : c;  // use forward slashes — cross-platform
        }
        std::fprintf(stdout,
                     "AYTHER_STATUS {\"event\":\"exit\",\"savestate\":\"%s\"}\n",
                     escaped.c_str());
    }

    // Stop the file watcher before any Rust/Vulkan teardown.
    pack_watcher.reset();

    // Export the tile catalog, then drop the session. Its destructor frees the
    // emulator host, every hasher/substitutor, the Lua script and the pack, and
    // shuts down audio — all via RAII (replaces the hand-written teardown).
    std::fprintf(stdout, "[main] Dumping tiles → tile_catalog.toml\n");
    sess->dump_tile_catalog("tile_catalog.toml");
    sess.reset();

    // Destroy Vulkan objects in reverse creation order.
    // vkDeviceWaitIdle before any teardown.
    if (vulkan.is_ready()) {
        vkDeviceWaitIdle(vulkan.device());
        overlay.shutdown(vulkan);           // ImGui + overlay render pass + FBs (M4)
        postprocess.shutdown(vulkan);       // CRT presentation pass (frontend)
        renderer.shutdown(vulkan.engine_view()); // Engine offscreen resources
        swapchain.shutdown();
    }
    vulkan.shutdown();   // safe no-op if never initialized
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
