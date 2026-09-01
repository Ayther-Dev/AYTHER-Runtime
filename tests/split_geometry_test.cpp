// ---------------------------------------------------------------------------
// split_geometry_test.cpp — #298: dónde cae cada mitad de la comparación.
//
// Es la parte del split que se equivoca: mitades corridas, divisor invertido, o
// una región de ancho 0 que Vulkan rechaza y que se manifiesta como una
// pantalla negra sin mensaje. Nada de eso necesita una GPU, y con la GPU
// adentro no se comprobaría nunca.
//
// Lo que sostiene la comparación no está acá —las dos mitades salen del mismo
// FrameView, y eso lo fija `ab_compare_smoke` (#305) por píxel—. Acá se fija
// que lo que se muestra de cada una sea SU parte y en SU lugar.
// ---------------------------------------------------------------------------
#include "vulkan_backend/split_geometry.h"

#include <cstdio>

namespace {

int g_pass = 0, g_fail = 0;
void check(bool ok, const char* what) {
    if (ok) ++g_pass; else ++g_fail;
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
}

}  // namespace

int main() {
    using ayther::split_region;
    std::printf("== split_geometry_test (#298) ==\n");

    // Un cuadro de 320×240 dibujado en un rect de 800×600 con margen (100,50).
    const int32_t SW = 320, SH = 240;
    const int32_t FX = 100, FY = 50, FW = 800, FH = 600;

    // -- 1. Las dos mitades cubren todo y no se pisan -----------------------
    std::printf("\n[1] las dos mitades cubren el cuadro exactamente\n");
    {
        const auto l = split_region(SW, SH, FX, FY, FW, FH, 0.0f, 0.5f, false);
        const auto r = split_region(SW, SH, FX, FY, FW, FH, 0.5f, 1.0f, false);
        check(l.valid && r.valid, "las dos son validas");
        check(l.dx0 == FX && r.dx1 == FX + FW,
              "juntas empiezan y terminan donde empezaria y terminaria la entera");
        check(l.dx1 == r.dx0, "se tocan sin pisarse ni dejar hueco");
        check(l.dy0 == FY && l.dy1 == FY + FH && r.dy0 == FY && r.dy1 == FY + FH,
              "las dos ocupan el alto completo");
    }

    // -- 2. Se recorta, no se escala ----------------------------------------
    std::printf("\n[2] cada mitad muestra SU parte, no el cuadro entero\n");
    {
        const auto l = split_region(SW, SH, FX, FY, FW, FH, 0.0f, 0.5f, false);
        // El error clásico: tomar la imagen ENTERA como fuente y meterla en
        // media pantalla. Ahí se compararían dos imagenes que ninguna de las
        // dos es.
        check(l.sx0 == 0 && l.sx1 == SW / 2,
              "la fuente se recorta a la mitad izquierda");
        check(l.sy0 == 0 && l.sy1 == SH, "…y al alto completo");
        // Y la proporcion se conserva: media fuente en medio destino.
        const double src_frac = double(l.sx1 - l.sx0) / SW;
        const double dst_frac = double(l.dx1 - l.dx0) / FW;
        check(src_frac > dst_frac - 0.01 && src_frac < dst_frac + 0.01,
              "…y ocupa la misma fraccion en origen y destino (sin deformar)");
    }

    // -- 3. El divisor en un extremo ----------------------------------------
    std::printf("\n[3] el divisor en un extremo no emite un blit vacio\n");
    {
        // Ancho 0 es un error de validacion de Vulkan y se ve como pantalla
        // negra sin mensaje. «No dibujar» es lo correcto: el otro lado ocupa
        // todo.
        check(!split_region(SW, SH, FX, FY, FW, FH, 0.0f, 0.0f, false).valid,
              "divisor pegado a la izquierda: la mitad izquierda no se dibuja");
        check(split_region(SW, SH, FX, FY, FW, FH, 0.0f, 1.0f, false).valid,
              "…y la derecha ocupa el cuadro entero");
        check(!split_region(SW, SH, FX, FY, FW, FH, 1.0f, 1.0f, false).valid,
              "y al reves");
        // Un tramo tan chico que no llega a un pixel de la FUENTE tampoco.
        check(!split_region(SW, SH, FX, FY, FW, FH, 0.0f, 0.001f, false).valid,
              "un tramo que no llega a un pixel de origen no se dibuja");
    }

    // -- 4. Vertical --------------------------------------------------------
    std::printf("\n[4] la orientacion vertical parte el otro eje\n");
    {
        const auto a = split_region(SW, SH, FX, FY, FW, FH, 0.0f, 0.5f, true);
        const auto b = split_region(SW, SH, FX, FY, FW, FH, 0.5f, 1.0f, true);
        check(a.valid && b.valid, "las dos son validas");
        check(a.sy0 == 0 && a.sy1 == SH / 2 && b.sy0 == SH / 2 && b.sy1 == SH,
              "se parte el ALTO, no el ancho");
        check(a.sx0 == 0 && a.sx1 == SW && b.sx0 == 0 && b.sx1 == SW,
              "…y las dos ocupan el ancho completo");
        check(a.dy1 == b.dy0 && a.dx0 == FX && a.dx1 == FX + FW,
              "y en destino, igual");
        // Y NO es la misma region que la horizontal: si lo fuera, el toggle de
        // orientacion no haria nada y nadie lo notaria hasta usarlo.
        const auto h = split_region(SW, SH, FX, FY, FW, FH, 0.0f, 0.5f, false);
        check(!(a.sx1 == h.sx1 && a.sy1 == h.sy1),
              "la orientacion CAMBIA la region (el toggle hace algo)");
    }

    // -- 5. Entradas absurdas -----------------------------------------------
    std::printf("\n[5] entradas imposibles no producen regiones imposibles\n");
    {
        check(!split_region(0, 0, FX, FY, FW, FH, 0.0f, 0.5f, false).valid,
              "una fuente vacia no se dibuja");
        check(!split_region(SW, SH, FX, FY, 0, 0, 0.0f, 0.5f, false).valid,
              "un destino vacio, tampoco");
        check(!split_region(SW, SH, FX, FY, FW, FH, 0.8f, 0.2f, false).valid,
              "un tramo invertido no se dibuja (no se «arregla» solo)");
        // Fuera de rango se recorta, no se propaga: un `split` de 1.5 vendria
        // de un arrastre fuera de la ventana.
        const auto c = split_region(SW, SH, FX, FY, FW, FH, -0.5f, 1.5f, false);
        check(c.valid && c.sx0 == 0 && c.sx1 == SW,
              "un tramo fuera de rango se recorta a [0,1]");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
