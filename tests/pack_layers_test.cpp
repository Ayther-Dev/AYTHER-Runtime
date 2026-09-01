// ---------------------------------------------------------------------------
// pack_layers_test.cpp — #561: el runtime arma el stack de Acetatos del pack.
//
// El defecto que cierra esta issue no era un cálculo equivocado: era que NADIE
// llamaba. La sesión leía los Acetatos del pack y los ofrecía, el Lab los
// dibujaba en Componer, y `ayther_runtime` —el que Play lanza— jugaba el pack
// sin ellos. Por eso el primer caso de este oráculo es el del DEFECTO: un stack
// por defecto (lo que el runtime pasaba, `layers = nullptr`) no tiene ni una
// capa Custom, aunque el pack traiga dos. Sin ese control, un test que sólo
// mira el stack construido pasa en verde aunque el runtime no lo use.
//
// Sin GPU: lo que se fija acá es el ORDEN (el del pack es back→front y el
// renderer dibuja en orden de lista) y que el contenido llegue COMPLETO — el
// renderer ya tiene sus propios oráculos con readback (acetato_fx_smoke).
// El determinismo del flicker y de la animación (#354/#489) se fija donde
// vive: son funciones puras del número de frame, y se las llama fuera de orden
// a propósito.
// ---------------------------------------------------------------------------
#include "pack_layers.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;
void check(bool ok, const char* what) {
    if (ok) ++g_pass; else ++g_fail;
    std::printf("  [%s] %s\n", ok ? " OK " : "FAIL", what);
}

size_t custom_count(const AytherLayerStack& s) {
    size_t n = 0;
    for (const AytherLayer& l : s.layers())
        if (l.kind == AytherLayerKind::Custom) ++n;
    return n;
}

/// Los Custom del stack, en orden de dibujo (back→front).
std::vector<const AytherLayer*> customs(const AytherLayerStack& s) {
    std::vector<const AytherLayer*> v;
    for (const AytherLayer& l : s.layers())
        if (l.kind == AytherLayerKind::Custom) v.push_back(&l);
    return v;
}

ayther::AytherSession::PackAcetato make(const char* name, const char* asset,
                                float factor, int16_t y) {
    ayther::AytherSession::PackAcetato a;
    a.name = name;
    std::snprintf(a.content.asset, sizeof(a.content.asset), "%s", asset);
    a.content.img_w  = 320;
    a.content.img_h  = 224;
    a.content.y      = y;
    a.content.factor = factor;
    return a;
}

}  // namespace

int main() {
    std::printf("== pack_layers_test (#561) ==\n");

    // -- El caso del DEFECTO: sin armar el stack, el pack se juega sin nada --
    {
        AytherLayerStack def;   // == lo que el renderer usa con layers=nullptr
        check(custom_count(def) == 0,
              "control: el stack por defecto no tiene NINGUNA capa Custom "
              "(el bug: un pack con Acetatos se jugaba sin ellos)");
    }

    // -- Un pack sin Acetatos no cambia un píxel -----------------------------
    {
        AytherLayerStack def, built;
        const size_t n = ayther_runtime::build_pack_acetato_stack({}, built);
        check(n == 0, "pack sin Acetatos: no inserta capas");
        bool same = def.layers().size() == built.layers().size();
        for (size_t i = 0; same && i < def.layers().size(); ++i)
            same = def.layers()[i].kind    == built.layers()[i].kind &&
                   def.layers()[i].visible == built.layers()[i].visible;
        check(same, "pack sin Acetatos: stack idéntico al por defecto");
    }

    // -- Dos Acetatos en paralaje: orden y contenido --------------------------
    {
        std::vector<ayther::AytherSession::PackAcetato> acs;
        acs.push_back(make("cielo",  "sky.png",   0.25f, 0));    // index 0 = atrás
        acs.push_back(make("nubes",  "cloud.png", 0.50f, 16));   // index 1 = delante
        acs[1].content.blend         = 1;      // aditivo
        acs[1].content.opacity       = 0.75f;
        acs[1].content.fit           = 1;
        acs[1].content.flicker_amp   = 0.2f;
        acs[1].content.flicker_ticks = 4;
        acs[1].content.tile_mode     = 1;
        acs[1].content.drift_x       = 6.0f;
        acs[1].content.pal_line      = 2;
        acs[1].content.add_screen(0xC0FFEEull);

        AytherLayerStack st;
        const size_t n = ayther_runtime::build_pack_acetato_stack(acs, st);
        check(n == 2, "dos Acetatos: inserta dos capas");
        check(custom_count(st) == 2, "dos Acetatos: dos capas Custom en el stack");

        const auto cs = customs(st);
        check(cs.size() == 2 &&
              std::strcmp(cs[0]->content.asset, "sky.png")   == 0 &&
              std::strcmp(cs[1]->content.asset, "cloud.png") == 0,
              "ORDEN back→front: el index 0 del pack queda DETRÁS del index 1");

        // El stack los deja delante de todas las lanes del renderer, que es
        // donde Componer los pone (insert_custom al final de la lista).
        const auto& L = st.layers();
        check(L[L.size() - 1].kind == AytherLayerKind::Custom &&
              L[L.size() - 2].kind == AytherLayerKind::Custom,
              "los Acetatos quedan al frente del stack, como en Componer");

        const AytherLayerContent& c = cs[1]->content;
        check(c.factor == 0.50f && c.y == 16 && c.img_w == 320 && c.img_h == 224,
              "contenido: parallax, posición y dimensiones");
        check(c.blend == 1 && c.opacity == 0.75f && c.fit == 1,
              "contenido: blend, opacidad y ajuste a pantalla");
        check(c.flicker_amp == 0.2f && c.flicker_ticks == 4 &&
              c.tile_mode == 1 && c.drift_x == 6.0f,
              "contenido: flicker, tileado y deriva");
        check(c.pal_line == 2 && c.gated() && c.has_screen(0xC0FFEEull),
              "contenido: tinte E1 y gate por Cuadro (#481/#482)");
        check(std::strcmp(cs[0]->name, "cielo") == 0 &&
              std::strcmp(cs[1]->name, "nubes") == 0,
              "el nombre del Acetato llega a la capa");
    }

    // -- «Se ve igual que en Componer» ---------------------------------------
    // El AC de #561 compara con el Lab. El renderer es una función del stack
    // (más el FrameView y el pack): si los dos frontends arman el MISMO stack,
    // dibujan el mismo píxel — y eso se puede fijar sin GPU. Acá se arma el
    // stack por el camino del Lab (insert_custom al final + set_content, que es
    // literalmente lo que hace host_componer.cpp) y se lo compara byte a byte
    // con el que arma el runtime desde el pack.
    {
        std::vector<ayther::AytherSession::PackAcetato> acs;
        acs.push_back(make("cielo", "sky.png",   0.25f, 0));
        acs.push_back(make("nubes", "cloud.png", 0.50f, 16));
        acs[1].content.blend       = 2;
        acs[1].content.opacity     = 0.4f;
        acs[1].content.anim_count  = 1;
        acs[1].content.anim_ticks  = 6;
        std::snprintf(acs[1].content.anim[0], sizeof(acs[1].content.anim[0]),
                      "%s", "cloud_b.png");

        AytherLayerStack runtime_stack;
        ayther_runtime::build_pack_acetato_stack(acs, runtime_stack);

        AytherLayerStack componer_stack;      // el camino del Lab, a mano
        for (const auto& a : acs) {
            const uint32_t id = componer_stack.insert_custom(
                a.name.c_str(), componer_stack.layers().size());
            componer_stack.set_visible(id, a.visible);
            componer_stack.set_content(id, a.content);
        }

        const auto r = customs(runtime_stack);
        const auto c = customs(componer_stack);
        bool equal = r.size() == c.size() &&
                     runtime_stack.layers().size() == componer_stack.layers().size();
        for (size_t i = 0; equal && i < r.size(); ++i)
            equal = r[i]->visible == c[i]->visible &&
                    std::strcmp(r[i]->name, c[i]->name) == 0 &&
                    std::memcmp(&r[i]->content, &c[i]->content,
                                sizeof(AytherLayerContent)) == 0;
        check(equal,
              "el stack del runtime es byte-idéntico al que arma Componer "
              "(mismo stack ⇒ mismo render)");
        // No vacuidad: que el contenido comparado no sea el default vacío.
        check(r.size() == 2 && r[1]->content.anim_count == 1 &&
              r[1]->content.blend == 2,
              "control de no vacuidad: se comparó contenido REAL, no defaults");
    }

    // -- visible = false: la capa entra apagada, y el renderer la saltea ------
    {
        std::vector<ayther::AytherSession::PackAcetato> acs;
        acs.push_back(make("apagado", "off.png", 0.5f, 0));
        acs[0].visible = false;
        acs.push_back(make("prendido", "on.png", 0.5f, 0));

        AytherLayerStack st;
        ayther_runtime::build_pack_acetato_stack(acs, st);
        const auto cs = customs(st);
        check(cs.size() == 2 && !cs[0]->visible && cs[1]->visible,
              "visible=false viaja del pack a la capa (el render corta en "
              "!L.visible)");
    }

    // -- Una ranura sin lámina no rompe el orden relativo ---------------------
    {
        std::vector<ayther::AytherSession::PackAcetato> acs;
        acs.push_back(make("fondo",  "back.png",  0.25f, 0));
        acs.push_back(make("ranura", "",          0.50f, 0));   // sin asset
        acs.push_back(make("frente", "front.png", 0.75f, 0));

        AytherLayerStack st;
        ayther_runtime::build_pack_acetato_stack(acs, st);
        const auto cs = customs(st);
        check(cs.size() == 3 &&
              std::strcmp(cs[0]->content.asset, "back.png")  == 0 &&
              cs[1]->content.asset[0] == 0 &&
              std::strcmp(cs[2]->content.asset, "front.png") == 0,
              "una ranura sin lámina conserva su lugar en el orden");
    }

    // -- Determinismo (#354/#489): seek atrás y pausa dan el mismo píxel ------
    {
        // Se las llama en orden y DESORDENADAS: si el flicker o el paso de
        // animación tuvieran un acumulador, retroceder daría otro valor — que
        // es exactamente el bug del tint rancio de la Panorámica en otra capa.
        const uint32_t frames[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
        float f_fwd[8]; uint32_t a_fwd[8];
        for (uint32_t i = 0; i < 8; ++i) {
            f_fwd[i] = acetato_flicker01(frames[i] / 4);
            a_fwd[i] = acetato_anim_step(frames[i], /*ticks=*/2, /*extra=*/2);
        }
        bool same = true;
        for (int i = 7; i >= 0; --i) {              // seek atrás
            same &= f_fwd[i] == acetato_flicker01(frames[i] / 4);
            same &= a_fwd[i] == acetato_anim_step(frames[i], 2, 2);
        }
        for (int r = 0; r < 3; ++r)                  // pausa: el mismo frame
            same &= f_fwd[5] == acetato_flicker01(frames[5] / 4) &&
                    a_fwd[5] == acetato_anim_step(frames[5], 2, 2);
        check(same, "flicker y animación son funciones PURAS del frame "
                    "(seek atrás y pausa dan el mismo valor)");
        // No vacuidad: si el flicker devolviera siempre lo mismo, el test de
        // arriba pasaría sin probar nada.
        bool varies = false;
        for (uint32_t i = 1; i < 8; ++i) varies |= f_fwd[i] != f_fwd[0];
        check(varies, "control de no vacuidad: el flicker efectivamente varía");
        check(a_fwd[0] == 0 && a_fwd[2] == 1 && a_fwd[4] == 2 && a_fwd[6] == 0,
              "control de no vacuidad: la animación cicla 0→1→2→0");
    }

    std::printf("== %d OK, %d FAIL ==\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
