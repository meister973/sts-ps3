/* GL prototypes for the @Import boundary. VitaGL20 calls standard GL entry
 * points by name; this header resolves them per platform:
 *   Vita  -> vitaGL (GLES2-level API over sceGxm)
 *   host  -> desktop OpenGL (Mesa exports all core 2.x entry points)
 * plus the one function that needs real glue (glShaderSource takes char**;
 * Java passes a single string). */

#ifndef STSV_GL_H
#define STSV_GL_H

#if defined(__vita__) || defined(VITA)
#include <vitaGL.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif

void stsv_glShaderSource(int shader, const void *source);

/* RGBA texture upload that transparently DXT5-compresses large atlases on the
 * Vita (they don't fit uncompressed in VRAM); plain glTexImage2D on the host. */
void stsv_tex_image_rgba(int target, int level, int width, int height, const void *rgba);
void stsv_tex_image_compressed(int target, int level, int internal_format,
                               int width, int height, int border,
                               int image_size, const void *blocks);
void stsv_tex_sub_image_rgba(int target, int level, int xoff, int yoff,
                             int width, int height, const void *rgba);

/* libGDX GL20 single-object convenience methods (not real GL entry points). */
int  stsv_gl_gen_texture(void);
void stsv_gl_delete_texture(int texture);
int  stsv_gl_gen_buffer(void);
void stsv_gl_delete_buffer(int buffer);
int  stsv_gl_gen_framebuffer(void);
void stsv_gl_delete_framebuffer(int framebuffer);
int  stsv_gl_gen_renderbuffer(void);
void stsv_gl_delete_renderbuffer(int renderbuffer);

#endif
