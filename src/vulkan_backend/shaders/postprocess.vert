#version 450
// ---------------------------------------------------------------------------
// postprocess.vert — fullscreen triangle for the post-process pass.
//
// Generates a screen-covering triangle from 3 vertices using the classic
// fullscreen-triangle trick: no vertex buffer required.
//
//   Vertex 0 →  NDC (-1, -1)  UV (0, 0)   TL
//   Vertex 1 →  NDC ( 3, -1)  UV (2, 0)   far right (outside screen)
//   Vertex 2 →  NDC (-1,  3)  UV (0, 2)   far bottom (outside screen)
//
// The triangle covers the entire NDC [-1,1]² viewport.  The visible UV
// range [0,1]² maps exactly to the swapchain surface.  The sampler uses
// CLAMP_TO_EDGE so the outside-triangle area never produces visible pixels.
//
// No push constants in this stage — all parameters are in the frag shader.
// ---------------------------------------------------------------------------

layout(location = 0) out vec2 frag_uv;

void main() {
    // frag_uv: (0,0) at TL, (1,1) at BR, (2,…) outside the visible square.
    frag_uv = vec2(
        (gl_VertexIndex == 1) ? 2.0 : 0.0,
        (gl_VertexIndex == 2) ? 2.0 : 0.0
    );

    // NDC position: map [0,1] UV → [-1,1] NDC then extend the triangle.
    gl_Position = vec4(frag_uv * 2.0 - 1.0, 0.0, 1.0);
}
