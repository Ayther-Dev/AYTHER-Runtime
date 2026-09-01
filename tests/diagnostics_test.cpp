// ---------------------------------------------------------------------------
// diagnostics_test.cpp — #302: el asistente de diagnóstico.
//
// La MEDICIÓN necesita SDL y una GPU. La DECISIÓN —qué perfil sugerir dados
// unos números— es pura, y es la que se equivoca: un orden de reglas mal puesto
// recomienda CRT en un equipo que no llega, o pixel-perfect en una pantalla
// donde la imagen queda diminuta.
//
// Y el otro check: **el informe dice que es una sugerencia**. Un asistente que
// se lee como una orden convierte una recomendación en una imposición — el
// mismo error que #296 evita con la precedencia.
// ---------------------------------------------------------------------------
#include "diagnostics.h"

#include <cstdio>
#include <string>

namespace {

int g_pass = 0, g_fail = 0;
void check(bool ok, const char* what) {
    if (ok) ++g_pass; else ++g_fail;
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
}
bool has(const std::string& h, const char* n) { return h.find(n) != std::string::npos; }

/// Un equipo holgado: Vulkan, sobra rendimiento, pantalla sin escala entera
/// cómoda (1366×768 no da ×3 de 320×240).
ayther::DiagnosticReport holgado() {
    ayther::DiagnosticReport r;
    r.vulkan = true;
    r.display_w = 1366; r.display_h = 768;
    r.refresh_hz = 60.0f;
    r.integer_scale = 2;
    r.frame_ms = 2.0f;
    r.audio_freq = 48000; r.audio_channels = 2;
    r.buffer_latency_ms = 10.6f;
    return r;
}

}  // namespace

int main() {
    using ayther::diagnose_suggest;
    std::printf("== diagnostics_test (#302) ==\n");

    // -- 1. Sin Vulkan, lo más simple ---------------------------------------
    std::printf("\n[1] sin Vulkan no se recomiendan shaders\n");
    {
        auto r = holgado();
        r.vulkan = false;
        diagnose_suggest(r);
        // Un CRT sobre un equipo que no puede ni presentar seria una
        // recomendacion que empeora las cosas.
        check(r.suggested == "lcd", "se sugiere LCD");
        check(!r.reason.empty(), "y se dice por que");
    }

    // -- 2. Rendimiento justo ------------------------------------------------
    std::printf("\n[2] con el equipo al limite, los shaders no entran\n");
    {
        auto r = holgado();
        r.frame_ms = 12.0f;    // de 16,6 disponibles
        diagnose_suggest(r);
        check(r.suggested == "lcd", "se sugiere LCD");
        // Sin fijar el redondeo del presupuesto (1000/60 = 16,7): lo que
        // importa es que el motivo traiga LO MEDIDO y no una frase generica.
        check(has(r.reason, "12.0") && has(r.reason, "16."),
              "el motivo trae los numeros medidos, no una frase generica");
    }

    // -- 3. El presupuesto sale del REFRESCO medido -------------------------
    std::printf("\n[3] el presupuesto depende del monitor, no de 60 fijo\n");
    {
        // 12 ms sobran a 60 Hz pero NO a 144: usar 60 fijo diria que hay margen
        // cuando no lo hay.
        auto a = holgado(); a.frame_ms = 6.0f; a.refresh_hz = 60.0f;
        diagnose_suggest(a);
        check(a.suggested != "lcd", "6 ms a 60 Hz: hay margen");

        auto b = holgado(); b.frame_ms = 6.0f; b.refresh_hz = 144.0f;
        diagnose_suggest(b);
        check(b.suggested == "lcd", "los MISMOS 6 ms a 144 Hz: no hay margen");
    }

    // -- 4. Pixel-perfect por geometría --------------------------------------
    std::printf("\n[4] con escala entera comoda, pixel-perfect\n");
    {
        auto r = holgado();
        r.integer_scale = 3;   // 960×720 en 1280×720
        diagnose_suggest(r);
        check(r.suggested == "pixel", "se sugiere pixel-perfect");
        check(has(r.reason, "3"), "y se dice cual es la escala");

        // El umbral es 3 y no 2: a ×2 la imagen queda tan chica que el
        // pixel-perfect se paga en tamaño mas de lo que rinde en nitidez.
        auto dos = holgado();
        dos.integer_scale = 2;
        diagnose_suggest(dos);
        check(dos.suggested != "pixel", "a ×2 NO se sugiere pixel-perfect");
    }

    // -- 5. El unico caso en que se sugiere un efecto -----------------------
    std::printf("\n[5] CRT solo cuando sobra equipo y la geometria no da\n");
    {
        auto r = holgado();
        diagnose_suggest(r);
        check(r.suggested == "crt", "equipo holgado + sin escala entera → CRT");
    }

    // -- 6. Lo que no se midio no se supone ---------------------------------
    std::printf("\n[6] una medicion que falta no se inventa\n");
    {
        // frame_ms = 0 significa «no se midio», no «tarda 0 ms». Tratarlo como
        // 0 recomendaria CRT en cualquier equipo del que no se pudo medir nada.
        ayther::DiagnosticReport r;
        r.vulkan = true;   // sin nada mas medido
        diagnose_suggest(r);
        check(!r.suggested.empty(), "igual sugiere algo");
        check(r.suggested != "pixel",
              "sin escala entera medida NO se sugiere pixel-perfect");
    }

    // -- 7. El informe -------------------------------------------------------
    std::printf("\n[7] el informe es local y dice que es una sugerencia\n");
    {
        auto r = holgado();
        diagnose_suggest(r);
        const std::string md = ayther::diagnostic_report_markdown(r);
        check(has(md, "local") && has(md, "no lo envía"),
              "dice que no sale de la maquina — no hay red en este modulo");
        check(has(md, "sugerencia") && has(md, "F4"),
              "dice que es una SUGERENCIA y como cambiarla");
        check(has(md, "1366") && has(md, "48000"), "trae lo medido");
        // El numero de latencia sin esta linea se lee como la latencia REAL, y
        // esa necesita un microfono para medirse.
        check(has(md, "micrófono"),
              "y aclara que la latencia del buffer no es la latencia total");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
