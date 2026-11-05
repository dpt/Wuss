/* wuss/crt.h -- CRT post-effect for the Wuss SDL frontend */

#ifndef WUSS_CRT_H
#define WUSS_CRT_H

#include <SDL3/SDL.h>

#include "base/result.h"

/* Renders the framebuffer through a CRT fragment shader (curvature, chroma
 * bleed, bloom, scanlines, vignette). Lifted from Chase H.Q.'s SDL3
 * frontend. Under Emscripten it draws with WebGL; otherwise via SDL's GPU API
 * with an MSL shader, so only on the Metal backend (macOS). Elsewhere
 * wuss_crt_create fails and the caller falls back to its plain SDL_Renderer
 * path.
 *
 * Owns the window's swapchain or GL context while it lives: an SDL_Renderer
 * must not be created on the same window. */

typedef struct wuss_crt wuss_crt_t;

/* Claim `window` for a new GPU device and build the shader pipeline. */
result_t wuss_crt_create(SDL_Window *window, wuss_crt_t **crt);

/* Upload `pixels` (width x height, tightly packed bgrx8888) and present one
 * frame through the shader, inset by `border` window units on every side. The
 * source texture is (re)created whenever the size changes. */
void wuss_crt_render(wuss_crt_t *crt,
                     SDL_Window *window,
                     const void *pixels,
                     int         width,
                     int         height,
                     int         border);

/* Release everything wuss_crt_create made and hand the window back. */
void wuss_crt_destroy(wuss_crt_t *crt, SDL_Window *window);

#endif /* WUSS_CRT_H */
