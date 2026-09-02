// ---------------------------------------------------------------------------
// diagnostics.cpp — ver diagnostics.h
// ---------------------------------------------------------------------------
#include "diagnostics.h"

#include <cstdio>
#include <sstream>
#include <string>

namespace ayther {

void diagnose_suggest(DiagnosticReport& r) {
    // 1. SIN VULKAN no hay nada que sugerir sobre shaders: lo primero es que se
    //    vea. Un CRT recomendado sobre un equipo que no puede ni presentar sería
    //    una recomendación que empeora las cosas.
    if (!r.vulkan) {
        r.suggested = "lcd";
        r.reason    = "Sin Vulkan disponible: se recomienda la salida más simple.";
        return;
    }

    // 2. RENDIMIENTO JUSTO. El presupuesto de un frame a 60 Hz es 16,6 ms; si la
    //    prueba ya come más de la mitad sin shaders, el CRT no entra.
    //    Se compara contra el refresco MEDIDO y no contra 60 fijo: en un monitor
    //    de 144 Hz el presupuesto es otro, y usar 60 diría que sobra tiempo
    //    cuando no sobra.
    const float budget_ms = r.refresh_hz > 1.0f ? (1000.0f / r.refresh_hz) : 16.6f;
    if (r.frame_ms > 0.0f && r.frame_ms > budget_ms * 0.5f) {
        r.suggested = "lcd";
        char b[192];
        std::snprintf(b, sizeof(b),
            "El equipo ya usa %.1f ms de los %.1f disponibles por frame: "
            "los shaders no entran.", r.frame_ms, budget_ms);
        r.reason = b;
        return;
    }

    // 3. PIXEL-PERFECT si la pantalla da para un múltiplo entero decente. Con
    //    ×3 o más, cada píxel del juego ocupa exactamente los mismos píxeles de
    //    pantalla y el dibujo deja de temblar al desplazarse.
    //
    //    El umbral es 3 y no 2: a ×2 el recorte respecto de la pantalla completa
    //    es tan grande que la imagen queda chica, y ahí el pixel-perfect se paga
    //    en tamaño más de lo que rinde en nitidez.
    if (r.integer_scale >= 3) {
        r.suggested = "pixel";
        char b[192];
        std::snprintf(b, sizeof(b),
            "La pantalla admite escala entera ×%u: cada píxel del juego cae "
            "exacto.", r.integer_scale);
        r.reason = b;
        return;
    }

    // 4. Con margen de sobra y sin escala entera cómoda, el CRT es lo que más
    //    se parece a como se veía. Es el único caso en que se sugiere un efecto:
    //    cuando sobra equipo y la geometría no ofrece nada mejor.
    r.suggested = "crt";
    r.reason    = "Hay margen de rendimiento y la pantalla no da escala entera "
                  "cómoda: el CRT simulado es lo más parecido al original.";
}

std::string diagnostic_report_markdown(const DiagnosticReport& r) {
    std::ostringstream o;
    o << "# Diagnóstico audiovisual\n\n";
    o << "Este informe es **local**. AYTHER no lo envía a ningún lado.\n\n";

    o << "## Pantalla\n\n";
    if (r.display_w && r.display_h)
        o << "- " << r.display_w << " × " << r.display_h << "\n";
    if (r.refresh_hz > 0.0f) {
        char b[48];
        std::snprintf(b, sizeof(b), "- %.1f Hz\n", r.refresh_hz);
        o << b;
    }
    if (r.integer_scale)
        o << "- escala entera máxima: ×" << r.integer_scale << "\n";
    o << "\n## Rendimiento\n\n";
    if (r.frame_ms > 0.0f) {
        char b[64];
        std::snprintf(b, sizeof(b), "- %.2f ms por frame\n", r.frame_ms);
        o << b;
    } else {
        o << "- no medido\n";
    }
    o << "\n## Audio\n\n";
    if (r.audio_freq) o << "- " << r.audio_freq << " Hz, "
                        << r.audio_channels << " canal(es)\n";
    if (r.buffer_latency_ms > 0.0f) {
        char b[128];
        std::snprintf(b, sizeof(b),
            "- buffer de salida: %.1f ms\n", r.buffer_latency_ms);
        o << b;
        // Se dice qué NO mide. Un número de latencia sin esta línea se lee como
        // la latencia real —de la muestra al parlante— y esa necesita un
        // micrófono para medirse.
        o << "\n  No es la latencia total: el driver, el sistema y el parlante "
             "suman lo suyo y eso sólo se mide con un micrófono.\n";
    }
    o << "\n## Gráficos\n\n";
    o << "- Vulkan: " << (r.vulkan ? "sí" : "no") << "\n";
    if (!r.gpu_name.empty()) o << "- " << r.gpu_name << "\n";

    o << "\n## Sugerencia\n\n";
    o << "**" << r.suggested << "** — " << r.reason << "\n\n";
    // Se dice explícitamente que es una sugerencia. Un asistente que aplica lo
    // que midió convierte una recomendación en una imposición.
    o << "Es una sugerencia: el perfil se cambia en cualquier momento con F4.\n";
    return o.str();
}

}  // namespace ayther
