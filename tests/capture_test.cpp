// ---------------------------------------------------------------------------
// capture_test.cpp — #300: captura comparativa sincronizada.
//
// El «mismo frame lógico» no se prueba acá: se consigue no teniendo dos
// capturas —las tres imágenes salen de un `FrameView` que `export_frame` no
// avanza— y eso lo sostiene el diseño, no un assert.
//
// Lo que SÍ se puede fijar sin GPU es lo que se equivoca:
//
//   · la composición del split: qué píxel viene de cuál de los dos;
//   · que la metadata NO lleve material protegido — el criterio de la issue —
//     y que distinga «sin perfil» de un perfil llamado "".
// ---------------------------------------------------------------------------
#include "capture.h"
#include "capture_service.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;
void check(bool ok, const char* what) {
    if (ok) ++g_pass; else ++g_fail;
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
}
bool has(const std::string& h, const char* n) { return h.find(n) != std::string::npos; }

/// Un buffer BGRA lleno de un color, para poder decir de dónde vino cada píxel.
std::vector<uint8_t> solid(uint32_t w, uint32_t h, uint8_t v) {
    return std::vector<uint8_t>(static_cast<size_t>(w) * h * 4, v);
}
uint8_t px(const std::vector<uint8_t>& b, uint32_t w, uint32_t x, uint32_t y) {
    return b[(static_cast<size_t>(y) * w + x) * 4];
}

ayther::CaptureWriteOperations real_operations;
int png_calls = 0;
int rename_calls = 0;

bool fail_second_png(const std::filesystem::path& path,
                     const std::uint8_t* pixels, const std::uint32_t width,
                     const std::uint32_t height) {
    ++png_calls;
    return png_calls != 2 &&
           real_operations.write_png(path, pixels, width, height);
}

bool fail_metadata(const std::filesystem::path&, std::string_view) {
    return false;
}

bool fail_second_rename(const std::filesystem::path& source,
                        const std::filesystem::path& destination) {
    ++rename_calls;
    return rename_calls != 2 &&
           real_operations.rename_file(source, destination);
}

bool capture_set_absent(const std::filesystem::path& directory,
                        const std::string& base) {
    for (const char* suffix :
         {"-original.png", "-ayther.png", "-split.png", ".json"}) {
        const auto final_path = directory / (base + suffix);
        auto temporary_path = final_path;
        temporary_path += ".tmp";
        if (std::filesystem::exists(final_path) ||
            std::filesystem::exists(temporary_path)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    std::printf("== capture_test (#300) ==\n");
    const uint32_t W = 16, H = 8;
    const auto A = solid(W, H, 0x11);   // «original»
    const auto B = solid(W, H, 0xEE);   // «ayther»

    // -- 1. La composición --------------------------------------------------
    std::printf("\n[1] cada mitad conserva SUS pixeles\n");
    {
        std::vector<uint8_t> out;
        check(ayther::compose_split(out, A.data(), B.data(), W, H, 0.5f, false),
              "compone");
        check(out.size() == static_cast<size_t>(W) * H * 4, "del tamano correcto");
        check(px(out, W, 0, 0) == 0x11 && px(out, W, W / 2 - 1, H - 1) == 0x11,
              "la mitad izquierda es el ORIGINAL, en todas las filas");
        check(px(out, W, W / 2, 0) == 0xEE && px(out, W, W - 1, H - 1) == 0xEE,
              "…y la derecha es AYTHER");
        // El error clasico: componer solo la primera fila y dejar el resto de
        // una de las dos. Se ve como una franja y se atribuye al emulador.
        bool todas = true;
        for (uint32_t y = 0; y < H; ++y)
            if (px(out, W, 0, y) != 0x11 || px(out, W, W - 1, y) != 0xEE) todas = false;
        check(todas, "…fila por fila, no solo la primera");
    }

    // -- 2. Los extremos ----------------------------------------------------
    std::printf("\n[2] el divisor en un extremo da una sola version\n");
    {
        std::vector<uint8_t> out;
        ayther::compose_split(out, A.data(), B.data(), W, H, 0.0f, false);
        check(px(out, W, 0, 0) == 0xEE && px(out, W, W - 1, 0) == 0xEE,
              "divisor a la izquierda: todo AYTHER");
        ayther::compose_split(out, A.data(), B.data(), W, H, 1.0f, false);
        check(px(out, W, 0, 0) == 0x11 && px(out, W, W - 1, 0) == 0x11,
              "divisor a la derecha: todo original");
        // Fuera de rango se recorta en vez de leer fuera del buffer.
        check(ayther::compose_split(out, A.data(), B.data(), W, H, 5.0f, false),
              "un divisor fuera de rango no revienta");
        check(px(out, W, W - 1, H - 1) == 0x11, "…se recorta a 1.0");
    }

    // -- 3. Vertical --------------------------------------------------------
    std::printf("\n[3] vertical parte el otro eje\n");
    {
        std::vector<uint8_t> out;
        ayther::compose_split(out, A.data(), B.data(), W, H, 0.5f, true);
        check(px(out, W, 0, 0) == 0x11 && px(out, W, W - 1, H / 2 - 1) == 0x11,
              "la mitad de ARRIBA es el original");
        check(px(out, W, 0, H / 2) == 0xEE && px(out, W, W - 1, H - 1) == 0xEE,
              "…y la de abajo es AYTHER");
    }

    // -- 4. Entradas invalidas ----------------------------------------------
    std::printf("\n[4] no se compone «lo que se pueda»\n");
    {
        // Media captura correcta y media basura es peor que ninguna: en el
        // explorador de archivos se ven iguales.
        std::vector<uint8_t> out;
        check(!ayther::compose_split(out, nullptr, B.data(), W, H, 0.5f, false),
              "sin una de las dos fuentes, no compone");
        check(!ayther::compose_split(out, A.data(), B.data(), 0, H, 0.5f, false),
              "con tamano cero, tampoco");
    }

    // -- 5. La metadata, que es el criterio de la issue ---------------------
    std::printf("\n[5] la metadata no lleva material protegido\n");
    {
        ayther::CaptureMeta m;
        m.game_id   = "crc32:7b905383";
        m.pack_name = "Golden Axe HD";
        m.pack_build = "9f2c1a";
        m.profile   = "enhanced";
        m.timestamp = "2026-08-14T21:30:00";
        m.width = 960; m.height = 720;
        const std::string j = ayther::capture_metadata_json(m);
        check(has(j, "crc32:7b905383") && has(j, "Golden Axe HD") && has(j, "9f2c1a"),
              "trae juego, pack y build id");
        check(has(j, "enhanced") && has(j, "2026-08-14T21:30:00"),
              "…perfil y timestamp");
        // Lo que NO puede traer: nada que identifique el archivo de ROM ni el
        // disco del autor. El game_id es un CRC, no una ruta.
        check(!has(j, ".md") && !has(j, ".bin") && !has(j, ":\\") && !has(j, "/home/"),
              "y NINGUNA ruta ni nombre de archivo de ROM");
        check(has(j, "\"capture\": \"ayther.compare\"") && has(j, "capture_version"),
              "se identifica y se versiona (lo consume el Lab, #310)");

        // Sin perfil = el estado no es ninguno declarado (#293). `null` y no
        // "": la captura tiene que poder decir «personalizado» sin inventar un
        // nombre de perfil que no existe.
        ayther::CaptureMeta n = m;
        n.profile.clear();
        check(has(ayther::capture_metadata_json(n), "\"profile\": null"),
              "sin perfil: null, no cadena vacia");
        // Y sin pack, igual: una sesion sin remasterizar tambien se captura.
        ayther::CaptureMeta s = m;
        s.pack_name.clear(); s.pack_build.clear();
        check(has(ayther::capture_metadata_json(s), "\"pack\": null"),
              "sin pack: null");
    }

    // -- 6. Overflow y commit transaccional (MAD-041) ----------------------
    std::printf("\n[6] la captura se publica como conjunto completo\n");
    {
        std::vector<uint8_t> out;
        const std::uint8_t one_pixel[4]{};
        check(!ayther::compose_split(
                  out, one_pixel, one_pixel,
                  std::numeric_limits<std::uint32_t>::max(), 2U, 0.5F, false),
              "dimensiones maliciosas fallan antes de multiplicar o reservar");

        namespace fs = std::filesystem;
        std::error_code filesystem_error;
        const fs::path directory =
            fs::temp_directory_path() / "ayther_capture_transaction_test";
        fs::remove_all(directory, filesystem_error);
        real_operations = ayther::default_capture_write_operations();

        ayther::CaptureMeta metadata;
        metadata.game_id = "crc32:test";
        metadata.width = W;
        metadata.height = H;
        const ayther::runtime::CaptureService service;
        const auto rejected = service.write(
            directory, "bad-size", W, H,
            std::span<const std::uint8_t>{A.data(), A.size() - 1}, B,
            0.5F, false, metadata);
        check(rejected.error == ayther::CaptureWriteError::invalid_input,
              "CaptureService rechaza buffers que no coinciden con el extent");
        const auto complete = ayther::capture_write_transactional(
            directory, "complete", A.data(), B.data(), W, H, 0.5F, false,
            metadata);
        check(static_cast<bool>(complete) &&
                  fs::exists(directory / "complete-original.png") &&
                  fs::exists(directory / "complete-ayther.png") &&
                  fs::exists(directory / "complete-split.png") &&
                  fs::exists(directory / "complete.json"),
              "PNG y metadata aparecen juntos tras cerrar todo");

        auto operations = real_operations;
        png_calls = 0;
        operations.write_png = &fail_second_png;
        const auto image_failure = ayther::capture_write_transactional(
            directory, "image-fail", A.data(), B.data(), W, H, 0.5F,
            false, metadata, operations);
        check(image_failure.error ==
                  ayther::CaptureWriteError::image_write_failed &&
                  capture_set_absent(directory, "image-fail"),
              "un fallo de imagen limpia temporales y no publica parciales");

        operations = real_operations;
        operations.write_text = &fail_metadata;
        const auto metadata_failure = ayther::capture_write_transactional(
            directory, "metadata-fail", A.data(), B.data(), W, H, 0.5F,
            false, metadata, operations);
        check(metadata_failure.error ==
                  ayther::CaptureWriteError::metadata_write_failed &&
                  capture_set_absent(directory, "metadata-fail"),
              "un fallo de metadata elimina las tres imagenes temporales");

        operations = real_operations;
        rename_calls = 0;
        operations.rename_file = &fail_second_rename;
        const auto rename_failure = ayther::capture_write_transactional(
            directory, "rename-fail", A.data(), B.data(), W, H, 0.5F,
            false, metadata, operations);
        check(rename_failure.error == ayther::CaptureWriteError::rename_failed &&
                  capture_set_absent(directory, "rename-fail"),
              "un fallo de rename revierte miembros ya publicados");

        fs::remove_all(directory, filesystem_error);
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
