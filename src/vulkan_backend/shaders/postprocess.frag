#version 450
// ---------------------------------------------------------------------------
// postprocess.frag — CRT-style post-processing over the emulator framebuffer.
//
// Samples the emulator framebuffer (emu_tex) and applies:
//   1. Scanlines   — horizontal dark lines at the native emulator resolution,
//                    simulating the phosphor gap of a CRT monitor.
//   2. Vignette    — darkens screen edges, common on CRT tubes.
//
// All effects gate on pc.crt_strength:
//   crt_strength = 0.0 → pure texture sample, no modification (passthrough).
//   crt_strength = 1.0 → full effect strength as set by scan / vignette params.
//
// Push constant layout (fragment stage, 8 × float = 32 bytes):
//   [0] scr_w        — swapchain width  in pixels
//   [1] scr_h        — swapchain height in pixels
//   [2] emu_h        — native emulator height in lines (e.g. 224 or 240)
//   [3] time         — elapsed time in seconds (for future animated effects)
//   [4] crt_strength — overall CRT effect intensity  [0, 1]
//   [5] scan_strength— scanline darkness              [0, 1]
//   [6] vignette     — vignette intensity             [0, 1]
//   [7] ntsc         — NTSC composite bleed            [0, 1]  (#230 EM-7.2)
// ---------------------------------------------------------------------------

layout(location = 0) in  vec2 frag_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D emu_tex;

layout(push_constant) uniform PC {
    float scr_w;
    float scr_h;
    float emu_h;
    float time;
    float crt_strength;
    float scan_strength;
    float vignette;
    float ntsc;
} pc;

// #230 EM-7.2 — NTSC: el SANGRADO DE CROMA de una senal compuesta.
//
// No es un decodificador NTSC de verdad (eso es Blargg: modular, muestrear la
// subportadora y demodular). Es lo que se VE de una senal compuesta en una tele:
// la luminancia llega nitida y el color, con un tercio del ancho de banda, se
// corre horizontalmente y se mezcla con el de al lado.
//
// Se hace en YIQ porque ahi luma y croma son ejes separados: promediar en RGB
// emborronaria tambien el brillo, que es exactamente lo que una senal compuesta
// NO hace, y el resultado se veria fuera de foco en vez de sangrado.
//
// La matriz es la de la norma. Ida y vuelta sin tocar nada devuelve el mismo
// color salvo epsilon de punto flotante, y de eso depende que `ntsc = 0` sea
// passthrough EXACTO.
vec3 rgb_a_yiq(vec3 c) {
    return vec3(dot(c, vec3(0.299,  0.587,  0.114)),
                dot(c, vec3(0.596, -0.274, -0.322)),
                dot(c, vec3(0.211, -0.523,  0.312)));
}
vec3 yiq_a_rgb(vec3 c) {
    return vec3(c.x + 0.956 * c.y + 0.621 * c.z,
                c.x - 0.272 * c.y - 0.647 * c.z,
                c.x - 1.106 * c.y + 1.703 * c.z);
}

void main() {
    vec4 color = texture(emu_tex, frag_uv);

    // #230 EM-7.2: el sangrado va PRIMERO, antes de scanlines y vineta. Es el
    // orden fisico: la senal llega degradada al tubo, y recien ahi el tubo le
    // pone su grilla de fosforo. Al reves, las scanlines se emborronarian.
    //
    // El gate es estricto (`> 0.0`) y no `> 0.001` como los otros: con NTSC
    // apagado la salida tiene que ser BIT-IDENTICA a no tener el efecto, y una
    // rama que corre con 0.0005 hace ida y vuelta por YIQ para nada — el
    // redondeo de esa vuelta ya no es identidad.
    if (pc.ntsc > 0.0) {
        float amp = clamp(pc.ntsc, 0.0, 1.0);
        // Un texel del framebuffer NATIVO, no de la pantalla: el sangrado es de
        // la senal, asi que su ancho no puede depender de a cuanto se escalo.
        float texel = 1.0 / max(pc.scr_w, 1.0);
        vec3 c0 = rgb_a_yiq(color.rgb);
        vec3 cl = rgb_a_yiq(texture(emu_tex, frag_uv - vec2(texel, 0.0)).rgb);
        vec3 cr = rgb_a_yiq(texture(emu_tex, frag_uv + vec2(texel, 0.0)).rgb);
        // La LUMA no se toca: es lo que separa el sangrado de una imagen
        // desenfocada.
        vec3 mezcla = vec3(c0.x,
                           (cl.y + c0.y + cr.y) / 3.0,
                           (cl.z + c0.z + cr.z) / 3.0);
        color.rgb = clamp(yiq_a_rgb(mix(c0, mezcla, amp)), 0.0, 1.0);
    }

    float effect = clamp(pc.crt_strength, 0.0, 1.0);

    if (effect > 0.001) {

        // ----------------------------------------------------------------
        // 1. Scanlines
        //    Dark line every other row at native emulator resolution.
        //    Uses emu_h so the gap aligns with the actual 224/240p lines
        //    regardless of the swapchain upscale factor.
        // ----------------------------------------------------------------
        if (pc.scan_strength > 0.001) {
            // Which emulator row does this pixel belong to?
            float emu_row = floor(frag_uv.y * pc.emu_h);
            // Odd rows get a dark gap; even rows stay bright.
            // scanline_factor: 1.0 on bright rows, (1 - scan_strength) on dark rows.
            float dark = 1.0 - clamp(pc.scan_strength, 0.0, 1.0);
            float scanline_factor = mix(1.0, dark, mod(emu_row, 2.0));
            color.rgb *= mix(1.0, scanline_factor, effect);
        }

        // ----------------------------------------------------------------
        // 2. Vignette
        //    Radial darkening centered on the screen.
        //    dist = squared distance from center [0..0.5], max at corners ≈ 0.5.
        // ----------------------------------------------------------------
        if (pc.vignette > 0.001) {
            vec2  center  = frag_uv - 0.5;
            float dist_sq = dot(center, center);     // 0 at center, ~0.5 at corners
            // Scale so vignette=1.0 reaches black at corners.
            float vig = 1.0 - clamp(dist_sq * pc.vignette * 4.0, 0.0, 1.0);
            color.rgb = mix(color.rgb, color.rgb * vig, effect);
        }
    }

    out_color = color;
}
