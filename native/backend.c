/* Aperture's native backend: a window, input, a batched 2D renderer, text, 3D meshes with
   lighting, render targets, shaders, and audio, on SDL3 and OpenGL 3.3 core.

   aperture.fw imports this file with `import c native.backend`; every non-static function
   below is a Flywheel function. Resources are named by small integers (0 is "none"), colors
   are four ints from 0 to 255, angles are degrees, and 2D coordinates have y pointing down.
   Failures raise NativeError in Flywheel through fw_foreign_fail. */

// flywheel: pkg sdl3 >= 3.2
// flywheel(unix): link m

#define _USE_MATH_DEFINES
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "font5x7.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_FAILURE_USERMSG
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "stb_image.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
/* Ogg Vorbis (stb_vorbis v1.24, public domain): .ogg sounds and streamed music. */
#define STB_VORBIS_NO_PUSHDATA_API
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-value"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "stb_vorbis.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif


void fw_foreign_fail(const char *message);

#ifndef APIENTRY
#define APIENTRY
#endif

/* --- OpenGL entry points -------------------------------------------------------------- */
/* Loaded through SDL after the context exists, so nothing here needs a GL loader library. */
#define AP_GL_FUNCTIONS(F)                                                                         \
    F(void, ActiveTexture, (GLenum))                                                               \
    F(void, AttachShader, (GLuint, GLuint))                                                        \
    F(void, BindBuffer, (GLenum, GLuint))                                                          \
    F(void, BindFramebuffer, (GLenum, GLuint))                                                     \
    F(void, BindRenderbuffer, (GLenum, GLuint))                                                    \
    F(void, BindTexture, (GLenum, GLuint))                                                         \
    F(void, BindVertexArray, (GLuint))                                                             \
    F(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum))                                   \
    F(void, BufferData, (GLenum, ptrdiff_t, const void *, GLenum))                                 \
    F(void, BufferSubData, (GLenum, ptrdiff_t, ptrdiff_t, const void *))                           \
    F(GLenum, CheckFramebufferStatus, (GLenum))                                                    \
    F(void, Clear, (GLbitfield))                                                                   \
    F(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                      \
    F(void, CompileShader, (GLuint))                                                               \
    F(GLuint, CreateProgram, (void))                                                               \
    F(GLuint, CreateShader, (GLenum))                                                              \
    F(void, CullFace, (GLenum))                                                                    \
    F(void, DeleteBuffers, (GLsizei, const GLuint *))                                              \
    F(void, DeleteFramebuffers, (GLsizei, const GLuint *))                                         \
    F(void, DeleteProgram, (GLuint))                                                               \
    F(void, DeleteRenderbuffers, (GLsizei, const GLuint *))                                        \
    F(void, DeleteShader, (GLuint))                                                                \
    F(void, DeleteTextures, (GLsizei, const GLuint *))                                             \
    F(void, DeleteVertexArrays, (GLsizei, const GLuint *))                                         \
    F(void, DepthFunc, (GLenum))                                                                   \
    F(void, DepthMask, (GLboolean))                                                                \
    F(void, Disable, (GLenum))                                                                     \
    F(void, DrawArrays, (GLenum, GLint, GLsizei))                                                  \
    F(void, DrawBuffer, (GLenum))                                                                  \
    F(void, DrawElements, (GLenum, GLsizei, GLenum, const void *))                                 \
    F(void, DrawElementsInstanced, (GLenum, GLsizei, GLenum, const void *, GLsizei))               \
    F(void, Enable, (GLenum))                                                                      \
    F(void, EnableVertexAttribArray, (GLuint))                                                     \
    F(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                             \
    F(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                         \
    F(void, GenBuffers, (GLsizei, GLuint *))                                                       \
    F(void, GenFramebuffers, (GLsizei, GLuint *))                                                  \
    F(void, GenRenderbuffers, (GLsizei, GLuint *))                                                 \
    F(void, GenTextures, (GLsizei, GLuint *))                                                      \
    F(void, GenVertexArrays, (GLsizei, GLuint *))                                                  \
    F(void, GenerateMipmap, (GLenum))                                                              \
    F(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, char *))                               \
    F(void, GetProgramiv, (GLuint, GLenum, GLint *))                                               \
    F(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, char *))                                \
    F(void, GetShaderiv, (GLuint, GLenum, GLint *))                                                \
    F(GLint, GetUniformLocation, (GLuint, const char *))                                           \
    F(void, LinkProgram, (GLuint))                                                                 \
    F(void, PixelStorei, (GLenum, GLint))                                                          \
    F(void, PolygonMode, (GLenum, GLenum))                                                         \
    F(void, PolygonOffset, (GLfloat, GLfloat))                                                     \
    F(void, ReadBuffer, (GLenum))                                                                  \
    F(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))                  \
    F(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                               \
    F(void, Scissor, (GLint, GLint, GLsizei, GLsizei))                                             \
    F(void, ShaderSource, (GLuint, GLsizei, const char *const *, const GLint *))                   \
    F(void, TexImage2D,                                                                            \
      (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))               \
    F(void, TexParameterfv, (GLenum, GLenum, const GLfloat *))                                     \
    F(void, TexParameteri, (GLenum, GLenum, GLint))                                                \
    F(void, TexSubImage2D,                                                                         \
      (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *))               \
    F(void, Uniform1f, (GLint, GLfloat))                                                           \
    F(void, Uniform1i, (GLint, GLint))                                                             \
    F(void, Uniform2f, (GLint, GLfloat, GLfloat))                                                  \
    F(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                         \
    F(void, Uniform3fv, (GLint, GLsizei, const GLfloat *))                                         \
    F(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                \
    F(void, Uniform4fv, (GLint, GLsizei, const GLfloat *))                                         \
    F(void, UniformMatrix3fv, (GLint, GLsizei, GLboolean, const GLfloat *))                        \
    F(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat *))                        \
    F(void, UseProgram, (GLuint))                                                                  \
    F(void, VertexAttribDivisor, (GLuint, GLuint))                                                 \
    F(void, VertexAttribIPointer, (GLuint, GLint, GLenum, GLsizei, const void *))                  \
    F(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *))        \
    F(void, Viewport, (GLint, GLint, GLsizei, GLsizei))

#define AP_GL_DECLARE(result, name, arguments) static result(APIENTRY *gl##name##_) arguments;
AP_GL_FUNCTIONS(AP_GL_DECLARE)

/* --- State ------------------------------------------------------------------------------ */
typedef struct {
    float x, y, u, v;
    unsigned char r, g, b, a;
} Vertex2;
/* tangent: xyz along +u, w the handedness of the bitangent; joints and weights skin a vertex
   to up to four bones of a skeleton (all weights zero leaves it unskinned). */
typedef struct {
    float x, y, z, nx, ny, nz, u, v;
    unsigned char r, g, b, a;
    float tx, ty, tz, tw;
    unsigned short joints[4];
    float weights[4];
} Vertex3;
typedef struct {
    GLuint id;
    int width, height;
    bool used, flipped; /* flipped: a render target's texture, whose rows run bottom up */
} Texture;
typedef struct {
    float x, y, w, h, xoff, yoff, advance;
    bool present;
} Glyph;
typedef struct {
    bool used;
    int texture;
    float pixel_size, line_height;
    Glyph glyphs[256];
} Font;
typedef struct {
    bool used;
    GLuint vao, vbo, ebo;
    int index_count, vertex_count;
    float radius; /* of the bounding sphere around the origin */
    float low[3], high[3];
    bool skinned;
} Mesh;
/* A surface: PBR metallic-roughness, as in glTF. Colors are linear. Textures are ids in
   A.textures (0 for none); `owned` ones are freed with the material. */
enum { MAP_BASE, MAP_NORMAL, MAP_METAL_ROUGH, MAP_EMISSIVE, MAP_OCCLUSION, MAP_COUNT };
typedef struct {
    bool used, unlit, double_sided, blend;
    float color[4], emissive[3];
    float metallic, roughness, normal_scale, occlusion_strength, alpha_cutoff;
    int maps[MAP_COUNT];
    bool owned[MAP_COUNT];
} Material;
typedef struct {
    bool used;
    GLuint framebuffer, depth;
    int texture, width, height;
} Canvas;
typedef struct {
    bool used;
    GLuint program;
} Shader;
typedef struct {
    bool used;
    SDL_AudioSpec spec;
    Uint8 *data;
    Uint32 length;
} Sound;
typedef struct {
    SDL_AudioStream *stream;
    int sound;
    bool looping;
} Voice;

/* Uniforms of the 3D programs, looked up by name once per program. */
#define AP_UNIFORMS(F)                                                                             \
    F(model) F(stack) F(viewproj) F(normal) F(instanced) F(skinned) F(joints) F(light_viewproj)   \
    F(shadow_offset) F(texture) F(tint) F(lit) F(ambient) F(sun_direction) F(sun_color)           \
    F(light_count) F(light_position) F(light_color) F(eye) F(shininess) F(specular) F(fog_color)  \
    F(fog_range) F(pbr) F(metallic) F(roughness) F(emissive) F(normal_map) F(normal_scale)        \
    F(has_normal_map) F(metal_rough) F(emissive_map) F(occlusion_map) F(occlusion_strength)       \
    F(alpha_cutoff) F(shadow_map) F(shadows) F(shadow_strength) F(shadow_texel)
#define AP_UNIFORM_ENUM(name) U_##name,
enum { AP_UNIFORMS(AP_UNIFORM_ENUM) U_COUNT };
typedef struct {
    GLuint id;
    GLint u[U_COUNT];
} Program3;
struct Model;

#define AP_MAX_TEXTURES 1024
#define AP_MAX_FONTS 64
#define AP_MAX_MESHES 4096
#define AP_MAX_MATERIALS 1024
#define AP_MAX_MODELS 256
#define AP_MAX_JOINTS 256
#define AP_MAX_CANVASES 64
#define AP_MAX_SHADERS 64
#define AP_MAX_SOUNDS 256
#define AP_MAX_VOICES 32
#define AP_MAX_LIGHTS 8
#define AP_MAX_GAMEPADS 4
#define AP_STACK_DEPTH 32
#define AP_BATCH_VERTICES 16384
#define AP_KEYS 512

static struct {
    SDL_Window *window;
    SDL_GLContext context;
    bool open, closing, frame_started;
    int width, height;             /* the window in points */
    int pixel_width, pixel_height; /* its drawable in pixels */
    int virtual_width, virtual_height;
    int view_x, view_y, view_w, view_h; /* viewport of the current target, in pixels */
    float logical_w, logical_h;         /* coordinate space of the current target */
    /* time */
    Uint64 last_ticks, start_ticks;
    double delta, time, fps;
    int target_fps;
    long frames;
    /* input */
    bool keys[AP_KEYS], previous_keys[AP_KEYS];
    Uint32 buttons, previous_buttons;
    float mouse_x, mouse_y, mouse_dx, mouse_dy, wheel_x, wheel_y;
    char text[128];
    SDL_Gamepad *gamepads[AP_MAX_GAMEPADS];
    bool pad_buttons[AP_MAX_GAMEPADS][SDL_GAMEPAD_BUTTON_COUNT];
    bool previous_pad_buttons[AP_MAX_GAMEPADS][SDL_GAMEPAD_BUTTON_COUNT];
    /* 2D batch */
    GLuint vao2, vbo2, program2;
    GLint mvp2, texture2, time2, resolution2;
    Vertex2 *batch;
    int batch_count;
    GLuint batch_texture;
    int white, blend, shader;
    float projection[16];
    float transform[AP_STACK_DEPTH][6]; /* 2D affine: x' = a x + c y + e, y' = b x + d y + f */
    int transform_depth;
    float polygon[256][2];
    int polygon_count;
    /* 3D */
    bool in3d, lighting, wireframe, culling, depth_writing;
    Program3 lit3, depth3; /* the 3D shader, and the one that draws into the shadow map */
    GLuint vao3, vbo3;
    GLuint instance_vbo, joint_texture;
    int instance_capacity;
    float *instance_data;
    float view[16], proj3[16], viewproj[16], eye[3];
    /* shadows of the sun */
    bool shadows, shadow_pass, shadow_ready;
    int shadow_size;
    GLuint shadow_fbo, shadow_depth;
    float shadow_viewproj[16], shadow_strength, shadow_offset;
    float model[AP_STACK_DEPTH][16];
    int model_depth;
    float ambient[3], sun_direction[3], sun_color[3];
    float light_position[AP_MAX_LIGHTS][4]; /* w is the range */
    float light_color[AP_MAX_LIGHTS][3];
    bool light_on[AP_MAX_LIGHTS];
    float shininess, specular, fog_color[3], fog_start, fog_end;
    Vertex3 *batch3;
    int batch3_count;
    GLenum batch3_mode;
    GLuint batch3_texture;
    /* mesh under construction */
    Vertex3 *build_vertices;
    unsigned *build_indices;
    int build_vertex_count, build_vertex_capacity, build_index_count, build_index_capacity;
    unsigned char build_color[4];
    bool build_tangents; /* the loader gave tangents; otherwise ap_mesh_end computes them */
    /* pixel buffer under construction */
    unsigned char *pixels;
    int pixels_w, pixels_h;
    /* resources */
    Texture textures[AP_MAX_TEXTURES];
    Font fonts[AP_MAX_FONTS];
    Mesh meshes[AP_MAX_MESHES];
    Material materials[AP_MAX_MATERIALS];
    struct Model *models[AP_MAX_MODELS];
    Canvas canvases[AP_MAX_CANVASES];
    Shader shaders[AP_MAX_SHADERS];
    Sound sounds[AP_MAX_SOUNDS];
    Voice voices[AP_MAX_VOICES];
    int canvas; /* the render target being drawn to, or 0 */
    SDL_AudioDeviceID audio;
    float master_volume;
    float result[16]; /* values returned by ap_project, ap_screen_ray, and node transforms */
} A;

/* Counts windows opened, so handles from an earlier window are never freed in a later one. */
static long session_count;
long ap_session(void) {
    return A.open ? session_count : 0;
}
static void fail(const char *format, ...) {
    char message[512];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(message, sizeof message, format, arguments);
    va_end(arguments);
    fw_foreign_fail(message);
}
static bool ready(void) {
    if (!A.open)
        fail("no window is open; call aperture.open first");
    return A.open;
}
static unsigned char channel(long value) {
    return (unsigned char)(value < 0 ? 0 : value > 255 ? 255 : value);
}

/* --- Matrices (column-major, as OpenGL takes them) ---------------------------------------- */
static void m4_identity(float *m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1;
}
static void m4_multiply(float *out, const float *a, const float *b) {
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++)
            r[c * 4 + row] = a[0 * 4 + row] * b[c * 4 + 0] + a[1 * 4 + row] * b[c * 4 + 1] +
                             a[2 * 4 + row] * b[c * 4 + 2] + a[3 * 4 + row] * b[c * 4 + 3];
    memcpy(out, r, sizeof r);
}
static void m4_ortho(float *m, float left, float right, float bottom, float top, float near,
                     float far) {
    m4_identity(m);
    m[0] = 2 / (right - left);
    m[5] = 2 / (top - bottom);
    m[10] = -2 / (far - near);
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(far + near) / (far - near);
}
static void m4_perspective(float *m, float fov_degrees, float aspect, float near, float far) {
    float f = 1.0f / tanf(fov_degrees * (float)M_PI / 360.0f);
    memset(m, 0, 16 * sizeof(float));
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (far + near) / (near - far);
    m[11] = -1;
    m[14] = 2 * far * near / (near - far);
}
static void v3_normalize(float *v) {
    float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length > 1e-12f) {
        v[0] /= length;
        v[1] /= length;
        v[2] /= length;
    }
}
static void v3_cross(float *out, const float *a, const float *b) {
    float r[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    memcpy(out, r, sizeof r);
}
static void m4_look_at(float *m, const float *eye, const float *target, const float *up) {
    float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]}, s[3], u[3];
    v3_normalize(f);
    v3_cross(s, f, up);
    v3_normalize(s);
    v3_cross(u, s, f);
    m4_identity(m);
    m[0] = s[0], m[4] = s[1], m[8] = s[2];
    m[1] = u[0], m[5] = u[1], m[9] = u[2];
    m[2] = -f[0], m[6] = -f[1], m[10] = -f[2];
    m[12] = -(s[0] * eye[0] + s[1] * eye[1] + s[2] * eye[2]);
    m[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    m[14] = f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2];
}
static void m4_translate(float *m, float x, float y, float z) {
    float t[16];
    m4_identity(t);
    t[12] = x, t[13] = y, t[14] = z;
    m4_multiply(m, m, t);
}
static void m4_scale(float *m, float x, float y, float z) {
    float s[16];
    m4_identity(s);
    s[0] = x, s[5] = y, s[10] = z;
    m4_multiply(m, m, s);
}
static void m4_rotate(float *m, float degrees, float x, float y, float z) {
    float axis[3] = {x, y, z}, r[16];
    v3_normalize(axis);
    float angle = degrees * (float)M_PI / 180.0f, c = cosf(angle), s = sinf(angle), t = 1 - c;
    x = axis[0], y = axis[1], z = axis[2];
    m4_identity(r);
    r[0] = t * x * x + c, r[1] = t * x * y + s * z, r[2] = t * x * z - s * y;
    r[4] = t * x * y - s * z, r[5] = t * y * y + c, r[6] = t * y * z + s * x;
    r[8] = t * x * z + s * y, r[9] = t * y * z - s * x, r[10] = t * z * z + c;
    m4_multiply(m, m, r);
}
static bool m4_invert(float *out, const float *m) {
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float determinant = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabsf(determinant) < 1e-20f)
        return false;
    for (int i = 0; i < 16; i++)
        out[i] = inv[i] / determinant;
    return true;
}
static void m4_transform(float *out, const float *m, const float *v) {
    float r[4];
    for (int row = 0; row < 4; row++)
        r[row] = m[row] * v[0] + m[4 + row] * v[1] + m[8 + row] * v[2] + m[12 + row] * v[3];
    memcpy(out, r, sizeof r);
}

/* --- Shaders ------------------------------------------------------------------------------ */
static const char *vertex2_source =
    "#version 330 core\n"
    "layout(location = 0) in vec2 a_position;\n"
    "layout(location = 1) in vec2 a_uv;\n"
    "layout(location = 2) in vec4 a_color;\n"
    "uniform mat4 u_mvp;\n"
    "out vec2 v_uv;\nout vec4 v_color;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n    v_color = a_color;\n"
    "    gl_Position = u_mvp * vec4(a_position, 0.0, 1.0);\n"
    "}\n";
static const char *fragment2_header =
    "#version 330 core\n"
    "in vec2 v_uv;\nin vec4 v_color;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_time;\n"
    "uniform vec2 u_resolution;\n"
    "out vec4 o_color;\n";
static const char *fragment2_default =
    "vec4 effect(vec4 color, sampler2D image, vec2 uv) { return texture(image, uv) * color; }\n";
static const char *fragment2_main = "void main() { o_color = effect(v_color, u_texture, v_uv); }\n";
/* One vertex shader for every 3D draw: plain, skinned (joint matrices in a float texture, four
   texels a joint), and instanced (a model matrix and a color per instance). */
static const char *vertex3_source =
    "#version 330 core\n"
    "layout(location = 0) in vec3 a_position;\n"
    "layout(location = 1) in vec3 a_normal;\n"
    "layout(location = 2) in vec2 a_uv;\n"
    "layout(location = 3) in vec4 a_color;\n"
    "layout(location = 4) in vec4 a_tangent;\n"
    "layout(location = 5) in uvec4 a_joints;\n"
    "layout(location = 6) in vec4 a_weights;\n"
    "layout(location = 7) in mat4 a_instance;\n"
    "layout(location = 11) in vec4 a_instance_color;\n"
    "uniform mat4 u_model;\nuniform mat4 u_stack;\nuniform mat4 u_viewproj;\n"
    "uniform mat3 u_normal;\nuniform int u_instanced;\nuniform int u_skinned;\n"
    "uniform sampler2D u_joints;\nuniform mat4 u_light_viewproj;\nuniform float u_shadow_offset;\n"
    "out vec3 v_world;\nout vec3 v_normal;\nout vec4 v_tangent;\nout vec2 v_uv;\n"
    "out vec4 v_color;\nout vec4 v_shadow;\n"
    "mat4 joint(uint j) {\n"
    "    int x = int(j) * 4;\n"
    "    return mat4(texelFetch(u_joints, ivec2(x, 0), 0), texelFetch(u_joints, ivec2(x + 1, 0), 0),\n"
    "                texelFetch(u_joints, ivec2(x + 2, 0), 0), texelFetch(u_joints, ivec2(x + 3, 0), 0));\n"
    "}\n"
    "void main() {\n"
    "    vec4 position = vec4(a_position, 1.0);\n"
    "    vec3 normal = a_normal, tangent = a_tangent.xyz;\n"
    "    if (u_skinned != 0) {\n"
    "        mat4 skin = a_weights.x * joint(a_joints.x) + a_weights.y * joint(a_joints.y) +\n"
    "                    a_weights.z * joint(a_joints.z) + a_weights.w * joint(a_joints.w);\n"
    "        position = skin * position;\n"
    "        normal = mat3(skin) * normal;\n"
    "        tangent = mat3(skin) * tangent;\n"
    "    }\n"
    "    mat4 model = u_model;\n"
    "    mat3 normal_matrix = u_normal;\n"
    "    v_color = a_color;\n"
    "    if (u_instanced != 0) {\n"
    "        model = u_stack * a_instance * u_model;\n"
    "        normal_matrix = transpose(inverse(mat3(model)));\n"
    "        v_color *= a_instance_color;\n"
    "    }\n"
    "    vec4 world = model * position;\n"
    "    v_world = world.xyz;\n"
    "    v_normal = normal_matrix * normal;\n"
    "    v_tangent = vec4(mat3(model) * tangent, a_tangent.w);\n"
    "    v_uv = a_uv;\n"
    "    float n = length(v_normal);\n"
    "    vec3 offset = n > 0.0 ? v_normal / n * u_shadow_offset : vec3(0.0);\n"
    "    v_shadow = u_light_viewproj * vec4(world.xyz + offset, 1.0);\n"
    "    gl_Position = u_viewproj * world;\n"
    "}\n";
/* Lighting: the original Blinn-Phong for plain draws, or PBR metallic-roughness (computed in
   linear light) for draws with a material. The sun casts shadows through the shadow map. */
static const char *fragment3_source =
    "#version 330 core\n"
    "in vec3 v_world;\nin vec3 v_normal;\nin vec4 v_tangent;\nin vec2 v_uv;\nin vec4 v_color;\n"
    "in vec4 v_shadow;\n"
    "uniform sampler2D u_texture;\nuniform sampler2D u_normal_map;\n"
    "uniform sampler2D u_metal_rough;\nuniform sampler2D u_emissive_map;\n"
    "uniform sampler2D u_occlusion_map;\nuniform sampler2DShadow u_shadow_map;\n"
    "uniform vec4 u_tint;\n"
    "uniform int u_lit;\nuniform int u_pbr;\nuniform int u_has_normal_map;\nuniform int u_shadows;\n"
    "uniform vec3 u_ambient;\nuniform vec3 u_sun_direction;\nuniform vec3 u_sun_color;\n"
    "uniform int u_light_count;\n"
    "uniform vec4 u_light_position[8];\nuniform vec3 u_light_color[8];\n"
    "uniform vec3 u_eye;\nuniform float u_shininess;\nuniform float u_specular;\n"
    "uniform vec3 u_fog_color;\nuniform vec2 u_fog_range;\n"
    "uniform float u_metallic;\nuniform float u_roughness;\nuniform vec3 u_emissive;\n"
    "uniform float u_normal_scale;\nuniform float u_occlusion_strength;\n"
    "uniform float u_alpha_cutoff;\nuniform float u_shadow_strength;\nuniform vec2 u_shadow_texel;\n"
    "out vec4 o_color;\n"
    "const float PI = 3.14159265;\n"
    "vec3 linear(vec3 c) { return pow(max(c, 0.0), vec3(2.2)); }\n"
    "float sunlit() {\n"
    "    if (u_shadows == 0) return 1.0;\n"
    "    vec3 p = v_shadow.xyz / v_shadow.w * 0.5 + 0.5;\n"
    "    if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;\n"
    "    float lit = 0.0;\n"
    "    for (int x = -1; x <= 1; x++)\n"
    "        for (int y = -1; y <= 1; y++)\n"
    "            lit += texture(u_shadow_map, vec3(p.xy + vec2(x, y) * u_shadow_texel, p.z - 0.0005));\n"
    "    return mix(1.0, lit / 9.0, u_shadow_strength);\n"
    "}\n"
    "vec3 surface_normal() {\n"
    "    vec3 n = normalize(v_normal);\n"
    "    if (!gl_FrontFacing) n = -n;\n"
    "    if (u_has_normal_map != 0 && dot(v_tangent.xyz, v_tangent.xyz) > 1e-10) {\n"
    "        vec3 t = normalize(v_tangent.xyz - n * dot(n, v_tangent.xyz));\n"
    "        vec3 b = cross(n, t) * (v_tangent.w < 0.0 ? -1.0 : 1.0);\n"
    "        vec3 m = texture(u_normal_map, v_uv).xyz * 2.0 - 1.0;\n"
    "        m.xy *= u_normal_scale;\n"
    "        n = normalize(mat3(t, b, n) * m);\n"
    "    }\n"
    "    return n;\n"
    "}\n"
    "vec3 brdf(vec3 n, vec3 v, vec3 l, vec3 albedo, float metal, float rough, vec3 radiance) {\n"
    "    float nl = dot(n, l);\n"
    "    if (nl <= 0.0) return vec3(0.0);\n"
    "    vec3 h = normalize(v + l);\n"
    "    float nv = max(dot(n, v), 1e-4), nh = max(dot(n, h), 0.0), vh = max(dot(v, h), 0.0);\n"
    "    float a = rough * rough, a2 = a * a;\n"
    "    float d = nh * nh * (a2 - 1.0) + 1.0;\n"
    "    float D = a2 / (PI * d * d);\n"
    "    float k = (rough + 1.0) * (rough + 1.0) / 8.0;\n"
    "    float G = nv / (nv * (1.0 - k) + k) * nl / (nl * (1.0 - k) + k);\n"
    "    vec3 f0 = mix(vec3(0.04), albedo, metal);\n"
    "    vec3 F = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);\n"
    "    vec3 specular = D * G * F / max(4.0 * nv * nl, 1e-4);\n"
    "    vec3 diffuse = (1.0 - F) * (1.0 - metal) * albedo / PI;\n"
    "    return (diffuse + specular) * radiance * nl;\n"
    "}\n"
    "void main() {\n"
    "    vec4 base = texture(u_texture, v_uv) * v_color * u_tint;\n"
    "    if (base.a < max(u_alpha_cutoff, 0.004)) discard;\n"
    "    vec3 color = base.rgb;\n"
    "    if (u_pbr != 0) {\n"
    "        vec3 albedo = linear(base.rgb);\n"
    "        color = albedo;\n"
    "        if (u_lit != 0) {\n"
    "            vec4 mr = texture(u_metal_rough, v_uv);\n"
    "            float rough = clamp(u_roughness * mr.g, 0.04, 1.0);\n"
    "            float metal = clamp(u_metallic * mr.b, 0.0, 1.0);\n"
    "            vec3 n = surface_normal();\n"
    "            vec3 v = normalize(u_eye - v_world);\n"
    "            float occlusion = mix(1.0, texture(u_occlusion_map, v_uv).r, u_occlusion_strength);\n"
    "            /* no environment map: metals reflect the ambient light and some sky */\n"
    "            vec3 f0 = mix(vec3(0.04), albedo, metal);\n"
    "            vec3 sky = linear(u_ambient) + linear(u_sun_color) * 0.25;\n"
    "            color = (linear(u_ambient) * albedo * (1.0 - metal) + sky * f0 * (1.0 - rough * 0.5)) *\n"
    "                    occlusion;\n"
    "            color += brdf(n, v, normalize(-u_sun_direction), albedo, metal, rough,\n"
    "                          linear(u_sun_color) * PI) * sunlit();\n"
    "            for (int i = 0; i < u_light_count; i++) {\n"
    "                vec3 delta = u_light_position[i].xyz - v_world;\n"
    "                float distance = length(delta);\n"
    "                float reach = clamp(1.0 - distance / u_light_position[i].w, 0.0, 1.0);\n"
    "                color += brdf(n, v, delta / max(distance, 0.0001), albedo, metal, rough,\n"
    "                              linear(u_light_color[i]) * PI * reach * reach);\n"
    "            }\n"
    "        }\n"
    "        color += u_emissive * linear(texture(u_emissive_map, v_uv).rgb);\n"
    "        color = pow(max(color, 0.0), vec3(1.0 / 2.2));\n"
    "    } else if (u_lit != 0) {\n"
    "        vec3 n = surface_normal();\n"
    "        vec3 to_eye = normalize(u_eye - v_world);\n"
    "        vec3 light = u_ambient;\n"
    "        vec3 shine = vec3(0.0);\n"
    "        vec3 l = normalize(-u_sun_direction);\n"
    "        float d = max(dot(n, l), 0.0);\n"
    "        float sun = d > 0.0 ? sunlit() : 1.0;\n"
    "        light += u_sun_color * d * sun;\n"
    "        if (d > 0.0 && u_specular > 0.0)\n"
    "            shine += u_sun_color * sun * pow(max(dot(n, normalize(l + to_eye)), 0.0), u_shininess);\n"
    "        for (int i = 0; i < u_light_count; i++) {\n"
    "            vec3 delta = u_light_position[i].xyz - v_world;\n"
    "            float distance = length(delta);\n"
    "            float reach = clamp(1.0 - distance / u_light_position[i].w, 0.0, 1.0);\n"
    "            reach *= reach;\n"
    "            l = delta / max(distance, 0.0001);\n"
    "            d = max(dot(n, l), 0.0) * reach;\n"
    "            light += u_light_color[i] * d;\n"
    "            if (d > 0.0 && u_specular > 0.0)\n"
    "                shine += u_light_color[i] * reach *\n"
    "                         pow(max(dot(n, normalize(l + to_eye)), 0.0), u_shininess);\n"
    "        }\n"
    "        color = color * light + shine * u_specular;\n"
    "    }\n"
    "    if (u_fog_range.y > u_fog_range.x) {\n"
    "        float fog = clamp((distance(u_eye, v_world) - u_fog_range.x) /\n"
    "                          (u_fog_range.y - u_fog_range.x), 0.0, 1.0);\n"
    "        color = mix(color, u_fog_color, fog);\n"
    "    }\n"
    "    o_color = vec4(color, base.a);\n"
    "}\n";
/* The shadow pass only writes depth; cut-out (alpha tested) surfaces leave holes. */
static const char *depth3_source =
    "#version 330 core\n"
    "in vec2 v_uv;\nin vec4 v_color;\n"
    "uniform sampler2D u_texture;\nuniform vec4 u_tint;\nuniform float u_alpha_cutoff;\n"
    "void main() {\n"
    "    float a = texture(u_texture, v_uv).a * v_color.a * u_tint.a;\n"
    "    if (a < (u_alpha_cutoff > 0.0 ? u_alpha_cutoff : 0.1)) discard;\n"
    "}\n";

static GLuint compile(GLenum kind, const char **parts, int count, char *log, int log_size) {
    GLuint shader = glCreateShader_(kind);
    glShaderSource_(shader, count, parts, NULL);
    glCompileShader_(shader);
    GLint ok = 0;
    glGetShaderiv_(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog_(shader, log_size, NULL, log);
        glDeleteShader_(shader);
        return 0;
    }
    return shader;
}
/* Links a program from vertex and fragment sources; 0 with `log` filled on failure. */
static GLuint program(const char **vertex, int vertex_count, const char **fragment,
                      int fragment_count, char *log, int log_size) {
    GLuint v = compile(GL_VERTEX_SHADER, vertex, vertex_count, log, log_size);
    if (!v)
        return 0;
    GLuint f = compile(GL_FRAGMENT_SHADER, fragment, fragment_count, log, log_size);
    if (!f) {
        glDeleteShader_(v);
        return 0;
    }
    GLuint p = glCreateProgram_();
    glAttachShader_(p, v);
    glAttachShader_(p, f);
    glLinkProgram_(p);
    glDeleteShader_(v);
    glDeleteShader_(f);
    GLint ok = 0;
    glGetProgramiv_(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog_(p, log_size, NULL, log);
        glDeleteProgram_(p);
        return 0;
    }
    return p;
}

/* --- Textures ------------------------------------------------------------------------------ */
static int texture_slot(void) {
    for (int i = 1; i < AP_MAX_TEXTURES; i++)
        if (!A.textures[i].used)
            return i;
    fail("too many textures (%d)", AP_MAX_TEXTURES - 1);
    return 0;
}
static Texture *texture_of(long id) {
    if (id <= 0 || id >= AP_MAX_TEXTURES || !A.textures[id].used)
        return NULL;
    return &A.textures[id];
}
static int texture_create(const unsigned char *rgba, int width, int height, bool smooth) {
    int slot = texture_slot();
    if (!slot)
        return 0;
    Texture *t = &A.textures[slot];
    glGenTextures_(1, &t->id);
    glBindTexture_(GL_TEXTURE_2D, t->id);
    glPixelStorei_(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D_(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    GLint filter = smooth ? GL_LINEAR : GL_NEAREST;
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    t->width = width;
    t->height = height;
    t->used = true;
    t->flipped = false;
    A.batch_texture = 0; /* the binding changed under the batch */
    return slot;
}

/* --- The 2D batch -------------------------------------------------------------------------- */
static void flush3(void);
static void use_blend(int mode) {
    switch (mode) {
    case 1: /* additive */
        glBlendFuncSeparate_(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ONE);
        break;
    case 2: /* multiply */
        glBlendFuncSeparate_(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case 3: /* premultiplied alpha */
        glBlendFuncSeparate_(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    default:
        glBlendFuncSeparate_(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                             GL_ONE_MINUS_SRC_ALPHA);
    }
}
static void flush(void) {
    if (!A.batch_count)
        return;
    GLuint active = A.program2;
    GLint mvp = A.mvp2, sampler = A.texture2, time = A.time2, resolution = A.resolution2;
    if (A.shader && A.shaders[A.shader].used) {
        active = A.shaders[A.shader].program;
        mvp = glGetUniformLocation_(active, "u_mvp");
        sampler = glGetUniformLocation_(active, "u_texture");
        time = glGetUniformLocation_(active, "u_time");
        resolution = glGetUniformLocation_(active, "u_resolution");
    }
    glUseProgram_(active);
    glUniformMatrix4fv_(mvp, 1, GL_FALSE, A.projection);
    glUniform1i_(sampler, 0);
    glUniform1f_(time, (float)A.time);
    glUniform2f_(resolution, A.logical_w, A.logical_h);
    glActiveTexture_(GL_TEXTURE0);
    glBindTexture_(GL_TEXTURE_2D, A.batch_texture);
    glBindVertexArray_(A.vao2);
    glBindBuffer_(GL_ARRAY_BUFFER, A.vbo2);
    glBufferData_(GL_ARRAY_BUFFER, AP_BATCH_VERTICES * sizeof(Vertex2), NULL, GL_STREAM_DRAW);
    glBufferSubData_(GL_ARRAY_BUFFER, 0, A.batch_count * sizeof(Vertex2), A.batch);
    glDrawArrays_(GL_TRIANGLES, 0, A.batch_count);
    A.batch_count = 0;
}
/* Makes room for `count` vertices drawn with texture `id` (a GL name). */
static Vertex2 *reserve(GLuint id, int count) {
    if (A.in3d)
        flush3();
    if (A.batch_texture != id || A.batch_count + count > AP_BATCH_VERTICES) {
        flush();
        A.batch_texture = id;
    }
    Vertex2 *out = A.batch + A.batch_count;
    A.batch_count += count;
    return out;
}
static void put(Vertex2 *v, float x, float y, float u, float w, const unsigned char *color) {
    const float *t = A.transform[A.transform_depth];
    v->x = t[0] * x + t[2] * y + t[4];
    v->y = t[1] * x + t[3] * y + t[5];
    v->u = u;
    v->v = w;
    v->r = color[0], v->g = color[1], v->b = color[2], v->a = color[3];
}
static GLuint white(void) {
    return A.textures[A.white].id;
}
static void quad(GLuint texture, const float *xy, const float *uv, const unsigned char *color) {
    Vertex2 *v = reserve(texture, 6);
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        int k = order[i];
        put(v + i, xy[k * 2], xy[k * 2 + 1], uv[k * 2], uv[k * 2 + 1], color);
    }
}
static const float unit_uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};

/* The projection and viewport of the current target: the window (letterboxed when a virtual
   size is set) or a canvas. */
static void set_target(void) {
    flush();
    if (A.canvas) {
        Canvas *c = &A.canvases[A.canvas];
        glBindFramebuffer_(GL_FRAMEBUFFER, c->framebuffer);
        A.view_x = A.view_y = 0;
        A.view_w = c->width;
        A.view_h = c->height;
        A.logical_w = (float)c->width;
        A.logical_h = (float)c->height;
    } else {
        glBindFramebuffer_(GL_FRAMEBUFFER, 0);
        A.view_x = A.view_y = 0;
        A.view_w = A.pixel_width;
        A.view_h = A.pixel_height;
        A.logical_w = (float)A.width;
        A.logical_h = (float)A.height;
        if (A.virtual_width > 0 && A.virtual_height > 0) {
            float scale = fminf((float)A.pixel_width / A.virtual_width,
                                (float)A.pixel_height / A.virtual_height);
            A.view_w = (int)(A.virtual_width * scale);
            A.view_h = (int)(A.virtual_height * scale);
            A.view_x = (A.pixel_width - A.view_w) / 2;
            A.view_y = (A.pixel_height - A.view_h) / 2;
            A.logical_w = (float)A.virtual_width;
            A.logical_h = (float)A.virtual_height;
        }
    }
    glViewport_(A.view_x, A.view_y, A.view_w, A.view_h);
    m4_ortho(A.projection, 0, A.logical_w, A.logical_h, 0, -1, 1);
}

/* --- 3D vertex layout ---------------------------------------------------------------------- */
static void lookup_uniforms(Program3 *p) {
#define AP_UNIFORM_LOOKUP(name) p->u[U_##name] = glGetUniformLocation_(p->id, "u_" #name);
    AP_UNIFORMS(AP_UNIFORM_LOOKUP)
}
/* Attributes 0 to 6 of Vertex3, from the bound array buffer, into the bound vertex array. */
static void vertex3_layout(void) {
    for (int i = 0; i < 7; i++)
        glEnableVertexAttribArray_(i);
    glVertexAttribPointer_(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3), (void *)0);
    glVertexAttribPointer_(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3),
                           (void *)offsetof(Vertex3, nx));
    glVertexAttribPointer_(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3),
                           (void *)offsetof(Vertex3, u));
    glVertexAttribPointer_(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex3),
                           (void *)offsetof(Vertex3, r));
    glVertexAttribPointer_(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3),
                           (void *)offsetof(Vertex3, tx));
    glVertexAttribIPointer_(5, 4, GL_UNSIGNED_SHORT, sizeof(Vertex3),
                            (void *)offsetof(Vertex3, joints));
    glVertexAttribPointer_(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex3),
                           (void *)offsetof(Vertex3, weights));
}
/* Attributes 7 to 11 (a model matrix and a color per instance) from the instance buffer. */
static void instance_layout(void) {
    glBindBuffer_(GL_ARRAY_BUFFER, A.instance_vbo);
    for (int i = 0; i < 5; i++) {
        glEnableVertexAttribArray_(7 + i);
        glVertexAttribPointer_(7 + i, 4, GL_FLOAT, GL_FALSE, 20 * sizeof(float),
                               (void *)(i * 4 * sizeof(float)));
        glVertexAttribDivisor_(7 + i, 1);
    }
}

/* --- Window and frames --------------------------------------------------------------------- */
static void refresh_size(void) {
    SDL_GetWindowSize(A.window, &A.width, &A.height);
    SDL_GetWindowSizeInPixels(A.window, &A.pixel_width, &A.pixel_height);
    if (A.width < 1)
        A.width = 1;
    if (A.height < 1)
        A.height = 1;
}
static void audio_close(void);
void ap_music_stop(void);
static void build_font(void);
static void model_release(struct Model *model);

/* flags: 1 resizable, 2 fullscreen, 4 no vsync, 8 hidden (for tests and tools), 16 4x
   multisampling, 32 borderless. */
void ap_open(const char *title, long width, long height, long flags) {
    if (A.open) {
        fail("a window is already open");
        return;
    }
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        fail("window size must be between 1 and 16384");
        return;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fail("cannot start the video system: %s", SDL_GetError());
        return;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    if (flags & 16) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    }
    SDL_WindowFlags window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (flags & 1)
        window_flags |= SDL_WINDOW_RESIZABLE;
    if (flags & 2)
        window_flags |= SDL_WINDOW_FULLSCREEN;
    if (flags & 8)
        window_flags |= SDL_WINDOW_HIDDEN;
    if (flags & 32)
        window_flags |= SDL_WINDOW_BORDERLESS;
    SDL_Window *window = SDL_CreateWindow(title, (int)width, (int)height, window_flags);
    if (!window) {
        fail("cannot create the window: %s", SDL_GetError());
        SDL_Quit();
        return;
    }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) {
        fail("cannot create an OpenGL 3.3 context: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    const char *missing = NULL;
#define AP_GL_LOAD(result, name, arguments)                                                        \
    gl##name##_ = (result(APIENTRY *) arguments)SDL_GL_GetProcAddress("gl" #name);                 \
    if (!gl##name##_ && !missing)                                                                  \
        missing = "gl" #name;
    AP_GL_FUNCTIONS(AP_GL_LOAD)
    if (missing) {
        fail("the OpenGL driver lacks %s (OpenGL 3.3 is required)", missing);
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    memset(&A, 0, sizeof A);
    A.window = window;
    A.context = context;
    A.open = true;
    session_count++;
    A.master_volume = 1;
    SDL_GL_SetSwapInterval(flags & 4 ? 0 : 1);
    refresh_size();

    char log[1024] = "";
    const char *vertex[] = {vertex2_source};
    const char *fragment[] = {fragment2_header, fragment2_default, fragment2_main};
    A.program2 = program(vertex, 1, fragment, 3, log, sizeof log);
    const char *vertex3[] = {vertex3_source};
    const char *fragment3[] = {fragment3_source};
    const char *depth3[] = {depth3_source};
    A.lit3.id = A.program2 ? program(vertex3, 1, fragment3, 1, log, sizeof log) : 0;
    A.depth3.id = A.lit3.id ? program(vertex3, 1, depth3, 1, log, sizeof log) : 0;
    if (!A.program2 || !A.lit3.id || !A.depth3.id) {
        fail("cannot build the built-in shaders: %s", log);
        A.open = false;
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }
    A.mvp2 = glGetUniformLocation_(A.program2, "u_mvp");
    A.texture2 = glGetUniformLocation_(A.program2, "u_texture");
    A.time2 = glGetUniformLocation_(A.program2, "u_time");
    A.resolution2 = glGetUniformLocation_(A.program2, "u_resolution");
    lookup_uniforms(&A.lit3);
    lookup_uniforms(&A.depth3);

    A.batch = malloc(AP_BATCH_VERTICES * sizeof(Vertex2));
    A.batch3 = malloc(AP_BATCH_VERTICES * sizeof(Vertex3));
    glGenVertexArrays_(1, &A.vao2);
    glBindVertexArray_(A.vao2);
    glGenBuffers_(1, &A.vbo2);
    glBindBuffer_(GL_ARRAY_BUFFER, A.vbo2);
    glBufferData_(GL_ARRAY_BUFFER, AP_BATCH_VERTICES * sizeof(Vertex2), NULL, GL_STREAM_DRAW);
    glEnableVertexAttribArray_(0);
    glVertexAttribPointer_(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2), (void *)0);
    glEnableVertexAttribArray_(1);
    glVertexAttribPointer_(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2),
                           (void *)offsetof(Vertex2, u));
    glEnableVertexAttribArray_(2);
    glVertexAttribPointer_(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex2),
                           (void *)offsetof(Vertex2, r));
    glGenVertexArrays_(1, &A.vao3);
    glBindVertexArray_(A.vao3);
    glGenBuffers_(1, &A.vbo3);
    glBindBuffer_(GL_ARRAY_BUFFER, A.vbo3);
    glBufferData_(GL_ARRAY_BUFFER, AP_BATCH_VERTICES * sizeof(Vertex3), NULL, GL_STREAM_DRAW);
    vertex3_layout();
    /* One instance buffer shared by every mesh's vertex array; never empty, so plain draws can
       read instance 0 from it harmlessly. */
    A.instance_capacity = 256;
    A.instance_data = malloc((size_t)A.instance_capacity * 20 * sizeof(float));
    glGenBuffers_(1, &A.instance_vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, A.instance_vbo);
    glBufferData_(GL_ARRAY_BUFFER, (ptrdiff_t)A.instance_capacity * 20 * sizeof(float), NULL,
                  GL_STREAM_DRAW);
    glBindVertexArray_(A.vao3);
    instance_layout();
    glBindVertexArray_(0);
    glGenTextures_(1, &A.joint_texture);
    glBindTexture_(GL_TEXTURE_2D, A.joint_texture);
    glTexImage2D_(GL_TEXTURE_2D, 0, 0x8814 /* GL_RGBA32F */, AP_MAX_JOINTS * 4, 1, 0, GL_RGBA,
                  GL_FLOAT, NULL);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    A.shadow_strength = 0.75f;

    static const unsigned char white_pixel[4] = {255, 255, 255, 255};
    A.white = texture_create(white_pixel, 1, 1, false);
    build_font();
    for (int i = 0; i < AP_STACK_DEPTH; i++) {
        A.transform[i][0] = A.transform[i][3] = 1;
        m4_identity(A.model[i]);
    }
    A.lighting = true;
    A.ambient[0] = A.ambient[1] = A.ambient[2] = 0.35f;
    A.sun_direction[0] = -0.4f, A.sun_direction[1] = -1.0f, A.sun_direction[2] = -0.6f;
    A.sun_color[0] = A.sun_color[1] = A.sun_color[2] = 0.8f;
    A.shininess = 32;
    A.build_color[0] = A.build_color[1] = A.build_color[2] = A.build_color[3] = 255;
    glEnable_(GL_BLEND);
    use_blend(0);
    glDisable_(GL_DEPTH_TEST);
    glDisable_(GL_CULL_FACE);
    if (flags & 16)
        glEnable_(0x809D /* GL_MULTISAMPLE */);
    set_target();
    A.start_ticks = A.last_ticks = SDL_GetPerformanceCounter();
    A.delta = 1.0 / 60.0;
    SDL_StartTextInput(window);
}
bool ap_is_open(void) {
    return A.open;
}
void ap_close(void) {
    if (!A.open)
        return;
    audio_close();
    for (int i = 0; i < AP_MAX_GAMEPADS; i++)
        if (A.gamepads[i])
            SDL_CloseGamepad(A.gamepads[i]);
    for (int i = 1; i < AP_MAX_TEXTURES; i++)
        if (A.textures[i].used)
            glDeleteTextures_(1, &A.textures[i].id);
    for (int i = 1; i < AP_MAX_MESHES; i++)
        if (A.meshes[i].used) {
            glDeleteVertexArrays_(1, &A.meshes[i].vao);
            glDeleteBuffers_(1, &A.meshes[i].vbo);
            glDeleteBuffers_(1, &A.meshes[i].ebo);
        }
    for (int i = 1; i < AP_MAX_CANVASES; i++)
        if (A.canvases[i].used) {
            glDeleteFramebuffers_(1, &A.canvases[i].framebuffer);
            glDeleteRenderbuffers_(1, &A.canvases[i].depth);
        }
    for (int i = 1; i < AP_MAX_SHADERS; i++)
        if (A.shaders[i].used)
            glDeleteProgram_(A.shaders[i].program);
    for (int i = 1; i < AP_MAX_SOUNDS; i++)
        if (A.sounds[i].used)
            SDL_free(A.sounds[i].data);
    for (int i = 1; i < AP_MAX_MODELS; i++)
        if (A.models[i])
            model_release(A.models[i]);
    if (A.shadow_fbo) {
        glDeleteFramebuffers_(1, &A.shadow_fbo);
        glDeleteTextures_(1, &A.shadow_depth);
    }
    glDeleteTextures_(1, &A.joint_texture);
    glDeleteBuffers_(1, &A.instance_vbo);
    free(A.instance_data);
    free(A.batch);
    free(A.batch3);
    free(A.build_vertices);
    free(A.build_indices);
    free(A.pixels);
    SDL_GL_DestroyContext(A.context);
    SDL_DestroyWindow(A.window);
    SDL_Quit();
    memset(&A, 0, sizeof A);
}
void ap_request_close(void) {
    A.closing = true;
}
static void audio_update(void);
void ap_screenshot(const char *path);
static void to_logical(float px, float py, float *x, float *y) {
    /* Window points to the coordinates of the window's drawing area: the same thing, unless
       a virtual size letterboxes it. */
    *x = px;
    *y = py;
    if (A.virtual_width <= 0 || A.virtual_height <= 0)
        return;
    double sx = (double)A.pixel_width / A.width, sy = (double)A.pixel_height / A.height;
    double scale = fmin((double)A.pixel_width / A.virtual_width,
                        (double)A.pixel_height / A.virtual_height);
    double ox = (A.pixel_width - A.virtual_width * scale) / 2;
    double oy = (A.pixel_height - A.virtual_height * scale) / 2;
    *x = (float)((px * sx - ox) / scale);
    *y = (float)((py * sy - oy) / scale);
}
static void handle(const SDL_Event *e) {
    switch (e->type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        A.closing = true;
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (e->key.scancode < AP_KEYS)
            A.keys[e->key.scancode] = e->type == SDL_EVENT_KEY_DOWN;
        break;
    case SDL_EVENT_MOUSE_MOTION: {
        float x, y, x0, y0;
        to_logical(e->motion.x, e->motion.y, &x, &y);
        to_logical(e->motion.x - e->motion.xrel, e->motion.y - e->motion.yrel, &x0, &y0);
        A.mouse_x = x;
        A.mouse_y = y;
        A.mouse_dx += x - x0;
        A.mouse_dy += y - y0;
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        A.buttons |= 1u << e->button.button;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        A.buttons &= ~(1u << e->button.button);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        A.wheel_x += e->wheel.x;
        A.wheel_y += e->wheel.y;
        break;
    case SDL_EVENT_TEXT_INPUT:
        strncat(A.text, e->text.text, sizeof A.text - strlen(A.text) - 1);
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        refresh_size();
        if (!A.canvas)
            set_target();
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        for (int i = 0; i < AP_MAX_GAMEPADS; i++)
            if (!A.gamepads[i]) {
                A.gamepads[i] = SDL_OpenGamepad(e->gdevice.which);
                break;
            }
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        for (int i = 0; i < AP_MAX_GAMEPADS; i++)
            if (A.gamepads[i] && SDL_GetGamepadID(A.gamepads[i]) == e->gdevice.which) {
                SDL_CloseGamepad(A.gamepads[i]);
                A.gamepads[i] = NULL;
            }
        break;
    }
}
/* Shows what the frame drew and keeps the frame rate. */
static void present(void) {
    if (A.in3d) {
        flush3();
        A.in3d = false;
        glDisable_(GL_DEPTH_TEST);
        glDisable_(GL_CULL_FACE);
    }
    flush();
    if (A.canvas) {
        A.canvas = 0;
        set_target();
    }
    SDL_GL_SwapWindow(A.window);
    if (A.target_fps > 0) {
        double budget = 1.0 / A.target_fps;
        double spent = (double)(SDL_GetPerformanceCounter() - A.last_ticks) /
                       (double)SDL_GetPerformanceFrequency();
        if (spent < budget)
            SDL_DelayPrecise((Uint64)((budget - spent) * 1e9));
    }
}
/* Seconds every frame counts as, whatever it really took (0: the real time). */
static double fixed_delta = 0;
void ap_fixed_delta(double seconds) {
    fixed_delta = seconds > 0 ? seconds : 0;
}
/* Starts a frame: presents the previous one, reads input, and advances the clock. Returns
   false once the window should close. */
bool ap_frame(void) {
    if (!ready())
        return false;
    /* APERTURE_FRAMES=n ends the loop after n frames, and APERTURE_SCREENSHOT=path saves the
       last one: for running a game unattended (tests, CI, thumbnails). */
    const char *limit = getenv("APERTURE_FRAMES");
    if (limit && *limit && A.frame_started && A.frames >= atol(limit)) {
        const char *shot = getenv("APERTURE_SCREENSHOT");
        if (shot && *shot && !A.closing)
            ap_screenshot(shot);
        A.closing = true;
    }
    if (A.frame_started)
        present();
    A.frame_started = true;
    Uint64 now = SDL_GetPerformanceCounter();
    A.delta = (double)(now - A.last_ticks) / (double)SDL_GetPerformanceFrequency();
    if (A.frames == 0)
        A.delta = A.target_fps > 0 ? 1.0 / A.target_fps : 1.0 / 60.0;
    if (A.delta > 0.25)
        A.delta = 0.25; /* a stall (a breakpoint, a dragged window) is not one long step */
    /* Unattended runs are deterministic: every frame is exactly one frame at the target rate,
       however fast the machine draws, so ticks and timers give the same result every run. */
    bool fixed = (limit && *limit) || fixed_delta > 0;
    if (fixed)
        A.delta = fixed_delta > 0 ? fixed_delta : A.target_fps > 0 ? 1.0 / A.target_fps : 1.0 / 60.0;
    A.last_ticks = now;
    A.time = fixed ? A.time + A.delta
                             : (double)(now - A.start_ticks) / (double)SDL_GetPerformanceFrequency();
    double instant = A.delta > 0 ? 1.0 / A.delta : 0;
    A.fps = A.frames == 0 ? instant : A.fps * 0.9 + instant * 0.1;
    A.frames++;

    memcpy(A.previous_keys, A.keys, sizeof A.keys);
    memcpy(A.previous_pad_buttons, A.pad_buttons, sizeof A.pad_buttons);
    A.previous_buttons = A.buttons;
    A.mouse_dx = A.mouse_dy = A.wheel_x = A.wheel_y = 0;
    A.text[0] = 0;
    SDL_Event event;
    while (SDL_PollEvent(&event))
        handle(&event);
    for (int i = 0; i < AP_MAX_GAMEPADS; i++)
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
            A.pad_buttons[i][b] =
                A.gamepads[i] && SDL_GetGamepadButton(A.gamepads[i], (SDL_GamepadButton)b);
    audio_update();
    A.transform_depth = 0;
    A.model_depth = 0;
    m4_identity(A.model[0]);
    A.shadow_ready = false; /* each frame draws its own shadows */
    set_target();
    return !A.closing;
}
double ap_delta(void) {
    return A.delta;
}
double ap_time(void) {
    return A.time;
}
double ap_fps(void) {
    return A.fps;
}
long ap_frame_count(void) {
    return A.frames;
}
void ap_target_fps(long fps) {
    A.target_fps = fps < 0 ? 0 : (int)fps;
}
void ap_vsync(bool on) {
    if (ready())
        SDL_GL_SetSwapInterval(on ? 1 : 0);
}
long ap_width(void) {
    return A.canvas ? A.canvases[A.canvas].width : A.virtual_width > 0 ? A.virtual_width : A.width;
}
long ap_height(void) {
    return A.canvas  ? A.canvases[A.canvas].height
           : A.virtual_height > 0 ? A.virtual_height
                                  : A.height;
}
long ap_window_width(void) {
    return A.width;
}
long ap_window_height(void) {
    return A.height;
}
/* Draw in a fixed coordinate space scaled to the window with bars; 0, 0 turns it off. */
void ap_virtual_size(long width, long height) {
    if (!ready())
        return;
    A.virtual_width = width > 0 ? (int)width : 0;
    A.virtual_height = height > 0 ? (int)height : 0;
    if (!A.canvas)
        set_target();
}
void ap_set_title(const char *title) {
    if (ready())
        SDL_SetWindowTitle(A.window, title);
}
void ap_set_size(long width, long height) {
    if (ready() && width > 0 && height > 0) {
        SDL_SetWindowSize(A.window, (int)width, (int)height);
        refresh_size();
        if (!A.canvas)
            set_target();
    }
}
void ap_fullscreen(bool on) {
    if (ready())
        SDL_SetWindowFullscreen(A.window, on);
}
bool ap_is_fullscreen(void) {
    return A.open && (SDL_GetWindowFlags(A.window) & SDL_WINDOW_FULLSCREEN) != 0;
}
void ap_show_cursor(bool on) {
    if (!ready())
        return;
    if (on)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}
/* Captures the mouse for camera control: the cursor is hidden and motion is unbounded. */
void ap_capture_mouse(bool on) {
    if (ready())
        SDL_SetWindowRelativeMouseMode(A.window, on);
}

/* When a file last changed, in seconds (an arbitrary epoch), or -1 when it cannot be read:
   for reloading frame data and other assets while the game runs. */
double ap_file_time(const char *path) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE)
        return -1;
    return (double)info.modify_time / 1e9;
}

/* --- Ticks -------------------------------------------------------------------------------- */
/* During a fixed-step tick (aperture.tick), "pressed" and "released" compare with the input of
   the previous tick instead of the previous frame: a press lands on exactly one tick, however
   many ticks a frame runs (none, when frames are faster than ticks; several, when slower). */
static struct {
    bool ticking;
    bool keys[AP_KEYS], frame_keys[AP_KEYS];
    bool pad[AP_MAX_GAMEPADS][SDL_GAMEPAD_BUTTON_COUNT];
    bool frame_pad[AP_MAX_GAMEPADS][SDL_GAMEPAD_BUTTON_COUNT];
    unsigned buttons, frame_buttons;
} T;
void ap_tick_begin(void) {
    if (T.ticking)
        return;
    memcpy(T.frame_keys, A.previous_keys, sizeof T.frame_keys);
    memcpy(A.previous_keys, T.keys, sizeof T.keys);
    memcpy(T.frame_pad, A.previous_pad_buttons, sizeof T.frame_pad);
    memcpy(A.previous_pad_buttons, T.pad, sizeof T.pad);
    T.frame_buttons = A.previous_buttons;
    A.previous_buttons = T.buttons;
    T.ticking = true;
}
void ap_tick_end(void) {
    if (!T.ticking)
        return;
    memcpy(T.keys, A.keys, sizeof T.keys);
    memcpy(A.previous_keys, T.frame_keys, sizeof T.frame_keys);
    memcpy(T.pad, A.pad_buttons, sizeof T.pad);
    memcpy(A.previous_pad_buttons, T.frame_pad, sizeof T.frame_pad);
    T.buttons = A.buttons;
    A.previous_buttons = T.frame_buttons;
    T.ticking = false;
}

/* --- Input --------------------------------------------------------------------------------- */
bool ap_key_down(long key) {
    return key >= 0 && key < AP_KEYS && A.keys[key];
}
bool ap_key_pressed(long key) {
    return key >= 0 && key < AP_KEYS && A.keys[key] && !A.previous_keys[key];
}
bool ap_key_released(long key) {
    return key >= 0 && key < AP_KEYS && !A.keys[key] && A.previous_keys[key];
}
/* The scancode of a key by name ("A", "Space", "Left"), or 0. */
long ap_key_code(const char *name) {
    return SDL_GetScancodeFromName(name);
}
const char *ap_key_name(long key) {
    return key > 0 && key < AP_KEYS ? SDL_GetScancodeName((SDL_Scancode)key) : "";
}
/* Any key that went down this frame, or 0. */
long ap_key_any(void) {
    for (int i = 1; i < AP_KEYS; i++)
        if (A.keys[i] && !A.previous_keys[i])
            return i;
    return 0;
}
const char *ap_text_input(void) {
    return A.text;
}
double ap_mouse_x(void) {
    return A.mouse_x;
}
double ap_mouse_y(void) {
    return A.mouse_y;
}
double ap_mouse_dx(void) {
    return A.mouse_dx;
}
double ap_mouse_dy(void) {
    return A.mouse_dy;
}
double ap_wheel_x(void) {
    return A.wheel_x;
}
double ap_wheel_y(void) {
    return A.wheel_y;
}
/* Buttons: 1 left, 2 middle, 3 right. */
bool ap_mouse_down(long button) {
    return button > 0 && button < 32 && (A.buttons >> button & 1);
}
bool ap_mouse_pressed(long button) {
    return button > 0 && button < 32 && (A.buttons >> button & 1) &&
           !(A.previous_buttons >> button & 1);
}
bool ap_mouse_released(long button) {
    return button > 0 && button < 32 && !(A.buttons >> button & 1) &&
           (A.previous_buttons >> button & 1);
}
bool ap_gamepad_connected(long pad) {
    return pad >= 0 && pad < AP_MAX_GAMEPADS && A.gamepads[pad] != NULL;
}
const char *ap_gamepad_name(long pad) {
    const char *name = ap_gamepad_connected(pad) ? SDL_GetGamepadName(A.gamepads[pad]) : NULL;
    return name ? name : "";
}
/* Axes: 0 left x, 1 left y, 2 right x, 3 right y, 4 left trigger, 5 right trigger; -1 to 1. */
double ap_gamepad_axis(long pad, long axis) {
    if (!ap_gamepad_connected(pad) || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
        return 0;
    double value = SDL_GetGamepadAxis(A.gamepads[pad], (SDL_GamepadAxis)axis) / 32767.0;
    if (value < -1)
        value = -1;
    return fabs(value) < 0.08 ? 0 : value; /* a small dead zone */
}
bool ap_gamepad_down(long pad, long button) {
    return pad >= 0 && pad < AP_MAX_GAMEPADS && button >= 0 &&
           button < SDL_GAMEPAD_BUTTON_COUNT && A.pad_buttons[pad][button];
}
bool ap_gamepad_pressed(long pad, long button) {
    return ap_gamepad_down(pad, button) && !A.previous_pad_buttons[pad][button];
}
void ap_gamepad_rumble(long pad, double low, double high, double seconds) {
    if (ap_gamepad_connected(pad))
        SDL_RumbleGamepad(A.gamepads[pad], (Uint16)(fmin(fmax(low, 0), 1) * 65535),
                          (Uint16)(fmin(fmax(high, 0), 1) * 65535), (Uint32)(seconds * 1000));
}
/* Synthetic input, for tests and replays: seen by the next frame like real input. */
void ap_inject_key(long key, bool down) {
    if (!ready() || key <= 0 || key >= AP_KEYS)
        return;
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = (SDL_Scancode)key;
    e.key.down = down;
    SDL_PushEvent(&e);
}
void ap_inject_mouse(double x, double y, long button, bool down) {
    if (!ready())
        return;
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.x = (float)x;
    e.motion.y = (float)y;
    SDL_PushEvent(&e);
    if (button > 0 && button < 32) {
        SDL_zero(e);
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.button = (Uint8)button;
        e.button.down = down;
        e.button.x = (float)x;
        e.button.y = (float)y;
        SDL_PushEvent(&e);
    }
}

/* --- 2D drawing ---------------------------------------------------------------------------- */
void ap_clear(long r, long g, long b, long a) {
    if (!ready())
        return;
    flush();
    A.batch_count = 0;
    glDepthMask_(GL_TRUE);
    glClearColor_(channel(r) / 255.0f, channel(g) / 255.0f, channel(b) / 255.0f,
                  channel(a) / 255.0f);
    glClear_(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
/* Blend modes: 0 alpha, 1 additive, 2 multiply, 3 premultiplied alpha. */
void ap_blend(long mode) {
    if (!ready() || mode == A.blend)
        return;
    flush();
    if (A.in3d)
        flush3();
    A.blend = (int)mode;
    use_blend(A.blend);
}
/* Limits drawing to a rectangle of the current target; a zero size removes the limit. */
void ap_scissor(double x, double y, double w, double h) {
    if (!ready())
        return;
    flush();
    if (w <= 0 || h <= 0) {
        glDisable_(GL_SCISSOR_TEST);
        return;
    }
    float sx = A.view_w / A.logical_w, sy = A.view_h / A.logical_h;
    /* A canvas is stored bottom up like the screen, so both flip the same way. */
    glEnable_(GL_SCISSOR_TEST);
    glScissor_(A.view_x + (int)(x * sx), A.view_y + A.view_h - (int)((y + h) * sy), (int)(w * sx),
               (int)(h * sy));
}
void ap_push(void) {
    if (A.transform_depth + 1 >= AP_STACK_DEPTH) {
        fail("too many nested transforms (%d)", AP_STACK_DEPTH);
        return;
    }
    memcpy(A.transform[A.transform_depth + 1], A.transform[A.transform_depth],
           sizeof A.transform[0]);
    A.transform_depth++;
}
void ap_pop(void) {
    if (A.transform_depth > 0)
        A.transform_depth--;
}
static void transform_apply(float a, float b, float c, float d, float e, float f) {
    float *t = A.transform[A.transform_depth], r[6];
    r[0] = t[0] * a + t[2] * b;
    r[1] = t[1] * a + t[3] * b;
    r[2] = t[0] * c + t[2] * d;
    r[3] = t[1] * c + t[3] * d;
    r[4] = t[0] * e + t[2] * f + t[4];
    r[5] = t[1] * e + t[3] * f + t[5];
    memcpy(t, r, sizeof r);
}
void ap_translate(double x, double y) {
    transform_apply(1, 0, 0, 1, (float)x, (float)y);
}
void ap_rotate(double degrees) {
    float angle = (float)(degrees * M_PI / 180.0), c = cosf(angle), s = sinf(angle);
    transform_apply(c, s, -s, c, 0, 0);
}
void ap_scale(double x, double y) {
    transform_apply((float)x, 0, 0, (float)y, 0, 0);
}
/* A 2D camera: the world point (target_x, target_y) appears at (offset_x, offset_y). */
void ap_camera(double target_x, double target_y, double offset_x, double offset_y,
               double rotation, double zoom) {
    ap_push();
    ap_translate(offset_x, offset_y);
    ap_rotate(rotation);
    ap_scale(zoom, zoom);
    ap_translate(-target_x, -target_y);
}
/* The point under screen position (x, y) in the current transform's coordinates. */
void ap_untransform(double x, double y) {
    const float *t = A.transform[A.transform_depth];
    float determinant = t[0] * t[3] - t[1] * t[2];
    if (fabsf(determinant) < 1e-12f) {
        A.result[0] = (float)x;
        A.result[1] = (float)y;
        return;
    }
    float px = (float)x - t[4], py = (float)y - t[5];
    A.result[0] = (t[3] * px - t[2] * py) / determinant;
    A.result[1] = (-t[1] * px + t[0] * py) / determinant;
}
double ap_result(long index) {
    return index >= 0 && index < 16 ? A.result[index] : 0;
}
#define AP_COLOR(name) unsigned char name[4] = {channel(r), channel(g), channel(b), channel(a)}
void ap_rect(double x, double y, double w, double h, long r, long g, long b, long a) {
    if (!ready())
        return;
    AP_COLOR(color);
    float xy[8] = {(float)x,       (float)y,       (float)(x + w), (float)y,
                   (float)(x + w), (float)(y + h), (float)x,       (float)(y + h)};
    quad(white(), xy, unit_uv, color);
}
/* A rectangle with a different color at each corner (top left, top right, bottom right,
   bottom left), each packed as 0xRRGGBBAA. */
void ap_rect_gradient(double x, double y, double w, double h, long top_left, long top_right,
                      long bottom_right, long bottom_left) {
    if (!ready())
        return;
    long packed[4] = {top_left, top_right, bottom_right, bottom_left};
    float xy[8] = {(float)x,       (float)y,       (float)(x + w), (float)y,
                   (float)(x + w), (float)(y + h), (float)x,       (float)(y + h)};
    Vertex2 *v = reserve(white(), 6);
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        int k = order[i];
        unsigned char color[4] = {(unsigned char)(packed[k] >> 24), (unsigned char)(packed[k] >> 16),
                                  (unsigned char)(packed[k] >> 8), (unsigned char)packed[k]};
        put(v + i, xy[k * 2], xy[k * 2 + 1], 0, 0, color);
    }
}
void ap_line(double x1, double y1, double x2, double y2, double thickness, long r, long g, long b,
             long a) {
    if (!ready())
        return;
    AP_COLOR(color);
    float dx = (float)(x2 - x1), dy = (float)(y2 - y1), length = sqrtf(dx * dx + dy * dy);
    if (length < 1e-6f)
        return;
    float nx = -dy / length * (float)thickness / 2, ny = dx / length * (float)thickness / 2;
    float xy[8] = {(float)x1 + nx, (float)y1 + ny, (float)x2 + nx, (float)y2 + ny,
                   (float)x2 - nx, (float)y2 - ny, (float)x1 - nx, (float)y1 - ny};
    quad(white(), xy, unit_uv, color);
}
void ap_rect_lines(double x, double y, double w, double h, double thickness, long r, long g,
                   long b, long a) {
    double t = thickness;
    ap_rect(x, y, w, t, r, g, b, a);
    ap_rect(x, y + h - t, w, t, r, g, b, a);
    ap_rect(x, y + t, t, h - 2 * t, r, g, b, a);
    ap_rect(x + w - t, y + t, t, h - 2 * t, r, g, b, a);
}
void ap_triangle(double x1, double y1, double x2, double y2, double x3, double y3, long r, long g,
                 long b, long a) {
    if (!ready())
        return;
    AP_COLOR(color);
    Vertex2 *v = reserve(white(), 3);
    put(v, (float)x1, (float)y1, 0, 0, color);
    put(v + 1, (float)x2, (float)y2, 0, 0, color);
    put(v + 2, (float)x3, (float)y3, 0, 0, color);
}
static int segments_for(double radius) {
    /* Enough segments that the edge looks round at the current scale. */
    const float *t = A.transform[A.transform_depth];
    float scale = sqrtf(fabsf(t[0] * t[3] - t[1] * t[2]));
    float pixels = (float)fabs(radius) * scale * (A.view_w / A.logical_w);
    int n = (int)(sqrtf(pixels) * 4.5f);
    return n < 12 ? 12 : n > 128 ? 128 : n;
}
/* A filled ellipse sector between two angles (degrees, clockwise from the +x axis). */
void ap_sector(double x, double y, double rx, double ry, double from, double to, long r, long g,
               long b, long a) {
    if (!ready())
        return;
    AP_COLOR(color);
    int n = segments_for(fmax(rx, ry));
    n = (int)ceil(n * fmin(fabs(to - from) / 360.0, 1.0));
    if (n < 2)
        n = 2;
    Vertex2 *v = reserve(white(), n * 3);
    double start = from * M_PI / 180.0, step = (to - from) * M_PI / 180.0 / n;
    for (int i = 0; i < n; i++) {
        double p = start + step * i, q = start + step * (i + 1);
        put(v++, (float)x, (float)y, 0, 0, color);
        put(v++, (float)(x + cos(p) * rx), (float)(y + sin(p) * ry), 0, 0, color);
        put(v++, (float)(x + cos(q) * rx), (float)(y + sin(q) * ry), 0, 0, color);
    }
}
void ap_circle(double x, double y, double radius, long r, long g, long b, long a) {
    ap_sector(x, y, radius, radius, 0, 360, r, g, b, a);
}
void ap_ellipse(double x, double y, double rx, double ry, long r, long g, long b, long a) {
    ap_sector(x, y, rx, ry, 0, 360, r, g, b, a);
}
/* A ring: the outline of an ellipse sector, `thickness` wide, centered on the radius. */
void ap_ring(double x, double y, double rx, double ry, double from, double to, double thickness,
             long r, long g, long b, long a) {
    if (!ready())
        return;
    AP_COLOR(color);
    int n = segments_for(fmax(rx, ry));
    n = (int)ceil(n * fmin(fabs(to - from) / 360.0, 1.0));
    if (n < 2)
        n = 2;
    double start = from * M_PI / 180.0, step = (to - from) * M_PI / 180.0 / n, half = thickness / 2;
    for (int i = 0; i < n; i++) {
        double p = start + step * i, q = start + step * (i + 1);
        float xy[8] = {(float)(x + cos(p) * (rx + half)), (float)(y + sin(p) * (ry + half)),
                       (float)(x + cos(q) * (rx + half)), (float)(y + sin(q) * (ry + half)),
                       (float)(x + cos(q) * (rx - half)), (float)(y + sin(q) * (ry - half)),
                       (float)(x + cos(p) * (rx - half)), (float)(y + sin(p) * (ry - half))};
        quad(white(), xy, unit_uv, color);
    }
}
void ap_circle_lines(double x, double y, double radius, double thickness, long r, long g, long b,
                     long a) {
    ap_ring(x, y, radius, radius, 0, 360, thickness, r, g, b, a);
}
/* Polygons: add points, then fill (convex) or stroke them. */
void ap_polygon_begin(void) {
    A.polygon_count = 0;
}
void ap_polygon_point(double x, double y) {
    if (A.polygon_count < 256) {
        A.polygon[A.polygon_count][0] = (float)x;
        A.polygon[A.polygon_count][1] = (float)y;
        A.polygon_count++;
    }
}
void ap_polygon_fill(long r, long g, long b, long a) {
    if (!ready() || A.polygon_count < 3)
        return;
    AP_COLOR(color);
    Vertex2 *v = reserve(white(), (A.polygon_count - 2) * 3);
    for (int i = 1; i + 1 < A.polygon_count; i++) {
        put(v++, A.polygon[0][0], A.polygon[0][1], 0, 0, color);
        put(v++, A.polygon[i][0], A.polygon[i][1], 0, 0, color);
        put(v++, A.polygon[i + 1][0], A.polygon[i + 1][1], 0, 0, color);
    }
}
void ap_polygon_stroke(double thickness, bool closed, long r, long g, long b, long a) {
    int n = A.polygon_count;
    for (int i = 0; i + 1 < n; i++)
        ap_line(A.polygon[i][0], A.polygon[i][1], A.polygon[i + 1][0], A.polygon[i + 1][1],
                thickness, r, g, b, a);
    if (closed && n > 2)
        ap_line(A.polygon[n - 1][0], A.polygon[n - 1][1], A.polygon[0][0], A.polygon[0][1],
                thickness, r, g, b, a);
}

/* --- Textures and images ------------------------------------------------------------------- */
static int texture_from_surface(SDL_Surface *loaded, bool smooth) {
    SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!rgba) {
        fail("cannot convert the image: %s", SDL_GetError());
        return 0;
    }
    unsigned char *tight = malloc((size_t)rgba->w * rgba->h * 4);
    for (int y = 0; y < rgba->h; y++)
        memcpy(tight + (size_t)y * rgba->w * 4, (unsigned char *)rgba->pixels + (size_t)y * rgba->pitch,
               (size_t)rgba->w * 4);
    int id = texture_create(tight, rgba->w, rgba->h, smooth);
    free(tight);
    SDL_DestroySurface(rgba);
    return id;
}
static SDL_Surface *load_surface(const char *path) {
    size_t length = strlen(path);
    bool png = length > 4 && SDL_strcasecmp(path + length - 4, ".png") == 0;
#if SDL_VERSION_ATLEAST(3, 4, 0)
    SDL_Surface *surface = png ? SDL_LoadPNG(path) : SDL_LoadBMP(path);
    if (!surface)
        fail("cannot load image '%s': %s", path, SDL_GetError());
#else
    SDL_Surface *surface = png ? NULL : SDL_LoadBMP(path);
    if (!surface)
        fail(png ? "cannot load '%s': PNG images need SDL 3.4 or newer (this is an older SDL); "
                   "use a BMP image"
                 : "cannot load image '%s': %s",
             path, SDL_GetError());
#endif
    return surface;
}
/* Loads a PNG or BMP image. `smooth` filters it when scaled; pixel art wants false. */
/* Loads a PNG, JPEG, BMP, or TGA image. */
long ap_texture_load(const char *path, bool smooth) {
    if (!ready())
        return 0;
    int width, height, channels;
    unsigned char *pixels = stbi_load(path, &width, &height, &channels, 4);
    if (pixels) {
        int id = width <= 16384 && height <= 16384 ? texture_create(pixels, width, height, smooth)
                                                   : 0;
        stbi_image_free(pixels);
        if (!id)
            fail("image '%s' is larger than 16384 pixels", path);
        return id;
    }
    SDL_Surface *surface = load_surface(path);
    return surface ? texture_from_surface(surface, smooth) : 0;
}
/* A texture from width * height RGBA bytes at `pixels` (the address of a buffer). */
long ap_texture_from_memory(const unsigned char *pixels, long width, long height, bool smooth) {
    if (!ready())
        return 0;
    if (!pixels || width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        fail("invalid texture data");
        return 0;
    }
    return texture_create(pixels, (int)width, (int)height, smooth);
}
long ap_texture_width(long id) {
    Texture *t = texture_of(id);
    return t ? t->width : 0;
}
long ap_texture_height(long id) {
    Texture *t = texture_of(id);
    return t ? t->height : 0;
}
void ap_texture_smooth(long id, bool smooth) {
    Texture *t = texture_of(id);
    if (!t)
        return;
    flush();
    glBindTexture_(GL_TEXTURE_2D, t->id);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, smooth ? GL_LINEAR : GL_NEAREST);
    A.batch_texture = 0;
}
void ap_texture_repeat(long id, bool repeat) {
    Texture *t = texture_of(id);
    if (!t)
        return;
    flush();
    glBindTexture_(GL_TEXTURE_2D, t->id);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    A.batch_texture = 0;
}
void ap_texture_free(long id) {
    Texture *t = texture_of(id);
    if (!t || id == A.white)
        return;
    flush();
    if (A.in3d)
        flush3();
    glDeleteTextures_(1, &t->id);
    memset(t, 0, sizeof *t);
    A.batch_texture = 0;
}
/* Draws the source rectangle of a texture into a destination rectangle, rotated by
   `rotation` degrees around the point (origin_x, origin_y) inside the destination. A negative
   source width or height mirrors the image. */
void ap_texture_draw(long id, double sx, double sy, double sw, double sh, double dx, double dy,
                     double dw, double dh, double origin_x, double origin_y, double rotation,
                     long r, long g, long b, long a) {
    if (!ready())
        return;
    Texture *t = texture_of(id);
    if (!t) {
        fail("drawing a texture that is not loaded (or was freed)");
        return;
    }
    AP_COLOR(color);
    float u0 = (float)(sx / t->width), v0 = (float)(sy / t->height);
    float u1 = (float)((sx + sw) / t->width), v1 = (float)((sy + sh) / t->height);
    if (t->flipped) {
        v0 = 1 - v0;
        v1 = 1 - v1;
    }
    float uv[8] = {u0, v0, u1, v0, u1, v1, u0, v1};
    float corners[8] = {(float)-origin_x,       (float)-origin_y,       (float)(dw - origin_x),
                        (float)-origin_y,       (float)(dw - origin_x), (float)(dh - origin_y),
                        (float)-origin_x,       (float)(dh - origin_y)};
    float angle = (float)(rotation * M_PI / 180.0), c = cosf(angle), s = sinf(angle), xy[8];
    for (int i = 0; i < 4; i++) {
        float px = corners[i * 2], py = corners[i * 2 + 1];
        xy[i * 2] = (float)dx + px * c - py * s;
        xy[i * 2 + 1] = (float)dy + px * s + py * c;
    }
    quad(t->id, xy, uv, color);
}
/* Pixel buffers: build an image in memory, then make a texture of it. */
void ap_pixels_begin(long width, long height) {
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) {
        fail("pixel buffer size must be between 1 and 8192");
        return;
    }
    free(A.pixels);
    A.pixels = calloc((size_t)width * height, 4);
    A.pixels_w = (int)width;
    A.pixels_h = (int)height;
}
void ap_pixels_set(long x, long y, long r, long g, long b, long a) {
    if (!A.pixels || x < 0 || y < 0 || x >= A.pixels_w || y >= A.pixels_h)
        return;
    unsigned char *p = A.pixels + ((size_t)y * A.pixels_w + x) * 4;
    p[0] = channel(r), p[1] = channel(g), p[2] = channel(b), p[3] = channel(a);
}
void ap_pixels_fill(long x, long y, long w, long h, long r, long g, long b, long a) {
    for (long row = y; row < y + h; row++)
        for (long column = x; column < x + w; column++)
            ap_pixels_set(column, row, r, g, b, a);
}
long ap_pixels_texture(bool smooth) {
    if (!ready())
        return 0;
    if (!A.pixels) {
        fail("no pixel buffer; call pixels_begin first");
        return 0;
    }
    return texture_create(A.pixels, A.pixels_w, A.pixels_h, smooth);
}
/* Replaces the pixels of an existing texture of the same size with the pixel buffer. */
void ap_pixels_update(long id) {
    Texture *t = texture_of(id);
    if (!t || !A.pixels || t->width != A.pixels_w || t->height != A.pixels_h) {
        fail("the pixel buffer and the texture must have the same size");
        return;
    }
    flush();
    glBindTexture_(GL_TEXTURE_2D, t->id);
    glPixelStorei_(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D_(GL_TEXTURE_2D, 0, 0, 0, t->width, t->height, GL_RGBA, GL_UNSIGNED_BYTE,
                     A.pixels);
    A.batch_texture = 0;
}

/* --- Reading the screen -------------------------------------------------------------------- */
/* The color drawn at a position of the current target, packed as 0xRRGGBBAA. */
long ap_read_pixel(double x, double y) {
    if (!ready())
        return 0;
    flush();
    if (A.in3d)
        flush3();
    int px = A.view_x + (int)(x * A.view_w / A.logical_w);
    int py = A.view_y + A.view_h - 1 - (int)(y * A.view_h / A.logical_h);
    unsigned char p[4] = {0, 0, 0, 0};
    glReadPixels_(px, py, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return (long)p[0] << 24 | (long)p[1] << 16 | (long)p[2] << 8 | p[3];
}
/* Saves the current target as a PNG (or a BMP, by extension). */
void ap_screenshot(const char *path) {
    if (!ready())
        return;
    flush();
    if (A.in3d)
        flush3();
    int w = A.view_w, h = A.view_h;
    unsigned char *pixels = malloc((size_t)w * h * 4), *row = malloc((size_t)w * 4);
    glPixelStorei_(GL_PACK_ALIGNMENT, 1);
    glReadPixels_(A.view_x, A.view_y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (int y = 0; y < h / 2; y++) { /* OpenGL rows run bottom up */
        unsigned char *top = pixels + (size_t)y * w * 4, *bottom = pixels + (size_t)(h - 1 - y) * w * 4;
        memcpy(row, top, (size_t)w * 4);
        memcpy(top, bottom, (size_t)w * 4);
        memcpy(bottom, row, (size_t)w * 4);
    }
    for (size_t i = 3; i < (size_t)w * h * 4; i += 4)
        pixels[i] = 255; /* the window's alpha is not part of the picture */
    SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels, w * 4);
    size_t length = strlen(path);
    bool bmp = length > 4 && SDL_strcasecmp(path + length - 4, ".bmp") == 0;
    bool saved = false;
    if (surface) {
#if SDL_VERSION_ATLEAST(3, 4, 0)
        saved = bmp ? SDL_SaveBMP(surface, path) : SDL_SavePNG(surface, path);
#else
        saved = SDL_SaveBMP(surface, path);
        (void)bmp;
#endif
    }
    if (!saved)
        fail("cannot save '%s': %s", path, SDL_GetError());
    SDL_DestroySurface(surface);
    free(pixels);
    free(row);
}

/* --- Canvases (render targets) ------------------------------------------------------------- */
long ap_canvas_create(long width, long height, bool smooth) {
    if (!ready())
        return 0;
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        fail("canvas size must be between 1 and 16384");
        return 0;
    }
    int slot = 0;
    for (int i = 1; i < AP_MAX_CANVASES && !slot; i++)
        if (!A.canvases[i].used)
            slot = i;
    if (!slot) {
        fail("too many canvases (%d)", AP_MAX_CANVASES - 1);
        return 0;
    }
    flush();
    Canvas *c = &A.canvases[slot];
    c->texture = texture_create(NULL, (int)width, (int)height, smooth);
    if (!c->texture)
        return 0;
    A.textures[c->texture].flipped = true;
    c->width = (int)width;
    c->height = (int)height;
    glGenFramebuffers_(1, &c->framebuffer);
    glBindFramebuffer_(GL_FRAMEBUFFER, c->framebuffer);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            A.textures[c->texture].id, 0);
    glGenRenderbuffers_(1, &c->depth);
    glBindRenderbuffer_(GL_RENDERBUFFER, c->depth);
    glRenderbufferStorage_(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, (int)width, (int)height);
    glFramebufferRenderbuffer_(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, c->depth);
    bool complete = glCheckFramebufferStatus_(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    c->used = true;
    set_target();
    if (!complete)
        fail("the graphics driver rejected a %ldx%ld canvas", width, height);
    return slot;
}
long ap_canvas_texture(long id) {
    return id > 0 && id < AP_MAX_CANVASES && A.canvases[id].used ? A.canvases[id].texture : 0;
}
/* Draw to a canvas (0 draws to the window again). */
void ap_canvas_target(long id) {
    if (!ready())
        return;
    if (id != 0 && (id < 0 || id >= AP_MAX_CANVASES || !A.canvases[id].used)) {
        fail("drawing to a canvas that does not exist (or was freed)");
        return;
    }
    if (A.in3d)
        flush3();
    flush();
    A.canvas = (int)id;
    set_target();
}
void ap_canvas_free(long id) {
    if (id <= 0 || id >= AP_MAX_CANVASES || !A.canvases[id].used)
        return;
    if (A.canvas == id)
        ap_canvas_target(0);
    Canvas *c = &A.canvases[id];
    glDeleteFramebuffers_(1, &c->framebuffer);
    glDeleteRenderbuffers_(1, &c->depth);
    ap_texture_free(c->texture);
    memset(c, 0, sizeof *c);
}

/* --- Shaders for 2D drawing ---------------------------------------------------------------- */
/* `source` defines `vec4 effect(vec4 color, sampler2D image, vec2 uv)` in GLSL; u_time and
   u_resolution are available, and uniforms it declares are set with ap_shader_set. */
long ap_shader_create(const char *source) {
    if (!ready())
        return 0;
    int slot = 0;
    for (int i = 1; i < AP_MAX_SHADERS && !slot; i++)
        if (!A.shaders[i].used)
            slot = i;
    if (!slot) {
        fail("too many shaders (%d)", AP_MAX_SHADERS - 1);
        return 0;
    }
    char log[1024] = "";
    const char *vertex[] = {vertex2_source};
    const char *fragment[] = {fragment2_header, source, "\n", fragment2_main};
    GLuint built = program(vertex, 1, fragment, 4, log, sizeof log);
    if (!built) {
        fail("shader error: %s", log);
        return 0;
    }
    A.shaders[slot].used = true;
    A.shaders[slot].program = built;
    return slot;
}
/* Draw with a shader (0 returns to plain drawing). */
void ap_shader_use(long id) {
    if (!ready() || id == A.shader)
        return;
    if (id != 0 && (id < 0 || id >= AP_MAX_SHADERS || !A.shaders[id].used)) {
        fail("using a shader that does not exist (or was freed)");
        return;
    }
    flush();
    A.shader = (int)id;
}
/* Sets a float, vec2, vec3, or vec4 uniform by its number of components. */
void ap_shader_set(long id, const char *name, long components, double x, double y, double z,
                   double w) {
    if (!ready() || id <= 0 || id >= AP_MAX_SHADERS || !A.shaders[id].used)
        return;
    flush();
    glUseProgram_(A.shaders[id].program);
    GLint location = glGetUniformLocation_(A.shaders[id].program, name);
    if (location < 0)
        return; /* unused uniforms are removed by the GLSL compiler */
    if (components <= 1)
        glUniform1f_(location, (float)x);
    else if (components == 2)
        glUniform2f_(location, (float)x, (float)y);
    else if (components == 3)
        glUniform3f_(location, (float)x, (float)y, (float)z);
    else
        glUniform4f_(location, (float)x, (float)y, (float)z, (float)w);
}
void ap_shader_free(long id) {
    if (id <= 0 || id >= AP_MAX_SHADERS || !A.shaders[id].used)
        return;
    if (A.shader == id)
        ap_shader_use(0);
    glDeleteProgram_(A.shaders[id].program);
    memset(&A.shaders[id], 0, sizeof A.shaders[id]);
}

/* --- Text ---------------------------------------------------------------------------------- */
/* The built-in font: every glyph in a 6x10 cell of a 96x60 atlas. Glyphs are 9 rows, but the
   font is 8 pixels tall (7 rows of letter and a gap) so descenders hang below the line. */
static void build_font(void) {
    enum { cell_w = 6, cell_h = 10, columns = 16, rows = 6, height = 8 };
    unsigned char *atlas = calloc(cell_w * columns * cell_h * rows, 4);
    Font *font = &A.fonts[0];
    memset(font, 0, sizeof *font);
    for (int index = 0; index < 95; index++) {
        int ox = index % columns * cell_w, oy = index / columns * cell_h;
        for (int y = 0; y < 9; y++)
            for (int x = 0; x < 5; x++)
                if (ap_font5x7[index][y] >> (4 - x) & 1) {
                    unsigned char *p = atlas + ((oy + y) * cell_w * columns + ox + x) * 4;
                    p[0] = p[1] = p[2] = p[3] = 255;
                }
        Glyph *glyph = &font->glyphs[32 + index];
        glyph->x = (float)ox, glyph->y = (float)oy;
        glyph->w = cell_w, glyph->h = 9;
        glyph->advance = cell_w;
        glyph->present = true;
    }
    font->texture = texture_create(atlas, cell_w * columns, cell_h * rows, false);
    font->pixel_size = height;
    font->line_height = height + 2;
    font->used = true;
    free(atlas);
}
static Font *font_of(long id) {
    return id >= 0 && id < AP_MAX_FONTS && A.fonts[id].used ? &A.fonts[id] : NULL;
}
/* A font over a texture atlas whose glyphs are `pixel_size` tall; add glyphs with
   ap_font_glyph. Font 0 is the built-in one. */
long ap_font_create(long texture, double pixel_size, double line_height) {
    if (!ready())
        return 0;
    if (!texture_of(texture) || pixel_size <= 0) {
        fail("a font needs a loaded texture and a positive size");
        return 0;
    }
    for (int i = 1; i < AP_MAX_FONTS; i++)
        if (!A.fonts[i].used) {
            memset(&A.fonts[i], 0, sizeof A.fonts[i]);
            A.fonts[i].used = true;
            A.fonts[i].texture = (int)texture;
            A.fonts[i].pixel_size = (float)pixel_size;
            A.fonts[i].line_height = (float)(line_height > 0 ? line_height : pixel_size * 1.2);
            return i;
        }
    fail("too many fonts (%d)", AP_MAX_FONTS - 1);
    return 0;
}
/* Where character `code` (0..255) lies in the atlas, and how it is placed and advanced. */
void ap_font_glyph(long id, long code, double x, double y, double w, double h, double xoff,
                   double yoff, double advance) {
    Font *font = font_of(id);
    if (!font || id == 0 || code < 0 || code > 255)
        return;
    Glyph *glyph = &font->glyphs[code];
    glyph->x = (float)x, glyph->y = (float)y, glyph->w = (float)w, glyph->h = (float)h;
    glyph->xoff = (float)xoff, glyph->yoff = (float)yoff, glyph->advance = (float)advance;
    glyph->present = true;
}
void ap_font_free(long id) {
    Font *font = font_of(id);
    if (font && id != 0)
        memset(font, 0, sizeof *font);
}
/* The next character of UTF-8 text; characters outside Latin-1 become '?'. */
static int next_code(const char **text) {
    const unsigned char *p = (const unsigned char *)*text;
    int code = *p++;
    if (code >= 0xF0 && p[0] && p[1] && p[2])
        code = '?', p += 3;
    else if (code >= 0xE0 && p[0] && p[1])
        code = '?', p += 2;
    else if (code >= 0xC0 && p[0]) {
        code = (code & 0x1F) << 6 | (p[0] & 0x3F);
        p += 1;
        if (code > 255)
            code = '?';
    }
    *text = (const char *)p;
    return code;
}
static const Glyph *glyph_of(const Font *font, int code) {
    if (code >= 0 && code < 256 && font->glyphs[code].present)
        return &font->glyphs[code];
    return font->glyphs['?'].present ? &font->glyphs['?'] : NULL;
}
/* Draws text with its top left corner at (x, y), `size` pixels tall; "\n" starts a line. */
void ap_text(long id, const char *text, double x, double y, double size, long r, long g, long b,
             long a) {
    if (!ready())
        return;
    Font *font = font_of(id);
    if (!font) {
        fail("drawing text with a font that is not loaded (or was freed)");
        return;
    }
    Texture *atlas = texture_of(font->texture);
    if (!atlas)
        return;
    AP_COLOR(color);
    float scale = (float)size / font->pixel_size, pen_x = (float)x, pen_y = (float)y;
    /* Glyph quads are snapped to device pixels, each with the same pixel size, so a pixel font
       at a fractional scale samples every copy of a letter the same way instead of splitting
       its texels differently wherever it lands. The pen itself is not rounded, so the text's
       width still matches ap_text_width. */
    float device_x = A.view_w / A.logical_w, device_y = A.view_h / A.logical_h;
    while (*text) {
        int code = next_code(&text);
        if (code == '\n') {
            pen_x = (float)x;
            pen_y += font->line_height * scale;
            continue;
        }
        const Glyph *glyph = glyph_of(font, code);
        if (!glyph)
            continue;
        if (glyph->w > 0 && glyph->h > 0 && code != ' ') {
            float left = roundf((pen_x + glyph->xoff * scale) * device_x) / device_x;
            float top = roundf((pen_y + glyph->yoff * scale) * device_y) / device_y;
            float right = left + fmaxf(1, roundf(glyph->w * scale * device_x)) / device_x;
            float bottom = top + fmaxf(1, roundf(glyph->h * scale * device_y)) / device_y;
            float xy[8] = {left, top, right, top, right, bottom, left, bottom};
            float u0 = glyph->x / atlas->width, v0 = glyph->y / atlas->height;
            float u1 = (glyph->x + glyph->w) / atlas->width;
            float v1 = (glyph->y + glyph->h) / atlas->height;
            float uv[8] = {u0, v0, u1, v0, u1, v1, u0, v1};
            quad(atlas->id, xy, uv, color);
        }
        pen_x += glyph->advance * scale;
    }
}
/* The width of the widest line of `text` at `size`. */
double ap_text_width(long id, const char *text, double size) {
    Font *font = font_of(id);
    if (!font)
        return 0;
    float scale = (float)size / font->pixel_size, width = 0, widest = 0;
    while (*text) {
        int code = next_code(&text);
        if (code == '\n') {
            width = 0;
            continue;
        }
        const Glyph *glyph = glyph_of(font, code);
        if (glyph)
            width += glyph->advance * scale;
        if (width > widest)
            widest = width;
    }
    return widest;
}
double ap_text_height(long id, const char *text, double size) {
    Font *font = font_of(id);
    if (!font)
        return 0;
    int lines = 1;
    for (; *text; text++)
        if (*text == '\n')
            lines++;
    float scale = (float)size / font->pixel_size;
    return (lines - 1) * font->line_height * scale + size;
}

/* --- 3D ------------------------------------------------------------------------------------ */
/* What one 3D draw call needs besides its vertices. */
typedef struct {
    const float *model; /* the model matrix; for instanced draws, what applies before each
                           instance's own matrix (the current 3D transform goes after it) */
    GLuint texture;     /* the texture of a draw without a material */
    const unsigned char *tint;
    bool lit, instanced, skinned;
    const Material *material; /* or NULL */
} Draw3;
static float to_srgb(float c) {
    return powf(c > 0 ? c : 0, 1 / 2.2f);
}
static GLuint map_texture(const Material *m, int map) {
    Texture *t = m && m->maps[map] > 0 && m->maps[map] < AP_MAX_TEXTURES &&
                         A.textures[m->maps[map]].used
                     ? &A.textures[m->maps[map]]
                     : NULL;
    return t ? t->id : 0;
}
static void upload3(const Draw3 *d) {
    Program3 *p = A.shadow_pass ? &A.depth3 : &A.lit3;
    const GLint *u = p->u;
    const Material *m = d->material;
    glUseProgram_(p->id);
    glUniformMatrix4fv_(u[U_model], 1, GL_FALSE, d->model);
    glUniformMatrix4fv_(u[U_stack], 1, GL_FALSE, A.model[A.model_depth]);
    glUniformMatrix4fv_(u[U_viewproj], 1, GL_FALSE, A.viewproj);
    /* Normals use the inverse transpose of the model's upper 3x3, so scaling keeps them right. */
    float inverse[16], normal[9];
    if (!m4_invert(inverse, d->model))
        m4_identity(inverse);
    for (int c = 0; c < 3; c++)
        for (int r = 0; r < 3; r++)
            normal[c * 3 + r] = inverse[r * 4 + c];
    glUniformMatrix3fv_(u[U_normal], 1, GL_FALSE, normal);
    glUniform1i_(u[U_instanced], d->instanced);
    glUniform1i_(u[U_skinned], d->skinned);
    float tint[4] = {d->tint[0] / 255.0f, d->tint[1] / 255.0f, d->tint[2] / 255.0f,
                     d->tint[3] / 255.0f};
    if (m) /* the material's linear color, back in the sRGB of the texture and the tint */
        for (int i = 0; i < 4; i++)
            tint[i] *= i < 3 ? to_srgb(m->color[i]) : m->color[3];
    glUniform4fv_(u[U_tint], 1, tint);
    glUniform1f_(u[U_alpha_cutoff], m ? m->alpha_cutoff : 0);
    glUniform1i_(u[U_texture], 0);
    glUniform1i_(u[U_normal_map], 1);
    glUniform1i_(u[U_metal_rough], 2);
    glUniform1i_(u[U_emissive_map], 3);
    glUniform1i_(u[U_occlusion_map], 4);
    glUniform1i_(u[U_shadow_map], 5);
    glUniform1i_(u[U_joints], 6);
    GLuint base = m ? map_texture(m, MAP_BASE) : d->texture;
    GLuint maps[5] = {base ? base : white(), map_texture(m, MAP_NORMAL),
                      map_texture(m, MAP_METAL_ROUGH), map_texture(m, MAP_EMISSIVE),
                      map_texture(m, MAP_OCCLUSION)};
    for (int i = 0; i < 5; i++) {
        glActiveTexture_(GL_TEXTURE0 + i);
        glBindTexture_(GL_TEXTURE_2D, maps[i] ? maps[i] : white());
    }
    glActiveTexture_(GL_TEXTURE6);
    glBindTexture_(GL_TEXTURE_2D, A.joint_texture);
    if (A.shadow_pass) {
        glActiveTexture_(GL_TEXTURE0);
        return;
    }
    bool shadowed = A.shadows && A.shadow_ready;
    glActiveTexture_(GL_TEXTURE5);
    glBindTexture_(GL_TEXTURE_2D, shadowed ? A.shadow_depth : 0);
    glActiveTexture_(GL_TEXTURE0);
    glUniform1i_(u[U_shadows], shadowed);
    if (shadowed) {
        glUniformMatrix4fv_(u[U_light_viewproj], 1, GL_FALSE, A.shadow_viewproj);
        glUniform1f_(u[U_shadow_offset], A.shadow_offset);
        glUniform1f_(u[U_shadow_strength], A.shadow_strength);
        glUniform2f_(u[U_shadow_texel], 1.0f / A.shadow_size, 1.0f / A.shadow_size);
    } else {
        float identity[16];
        m4_identity(identity);
        glUniformMatrix4fv_(u[U_light_viewproj], 1, GL_FALSE, identity);
        glUniform1f_(u[U_shadow_offset], 0);
    }
    glUniform1i_(u[U_lit], d->lit && A.lighting && !(m && m->unlit));
    glUniform1i_(u[U_pbr], m != NULL);
    if (m) {
        glUniform1f_(u[U_metallic], m->metallic);
        glUniform1f_(u[U_roughness], m->roughness);
        glUniform3fv_(u[U_emissive], 1, m->emissive);
        glUniform1f_(u[U_normal_scale], m->normal_scale);
        glUniform1i_(u[U_has_normal_map], maps[1] != 0);
        glUniform1f_(u[U_occlusion_strength], maps[4] ? m->occlusion_strength : 0);
    } else
        glUniform1i_(u[U_has_normal_map], 0);
    glUniform3fv_(u[U_ambient], 1, A.ambient);
    float direction[3] = {A.sun_direction[0], A.sun_direction[1], A.sun_direction[2]};
    v3_normalize(direction);
    glUniform3fv_(u[U_sun_direction], 1, direction);
    glUniform3fv_(u[U_sun_color], 1, A.sun_color);
    float positions[AP_MAX_LIGHTS * 4], colors[AP_MAX_LIGHTS * 3];
    int count = 0;
    for (int i = 0; i < AP_MAX_LIGHTS; i++)
        if (A.light_on[i]) {
            memcpy(positions + count * 4, A.light_position[i], 4 * sizeof(float));
            memcpy(colors + count * 3, A.light_color[i], 3 * sizeof(float));
            count++;
        }
    glUniform1i_(u[U_light_count], count);
    if (count) {
        glUniform4fv_(u[U_light_position], count, positions);
        glUniform3fv_(u[U_light_color], count, colors);
    }
    glUniform3fv_(u[U_eye], 1, A.eye);
    glUniform1f_(u[U_shininess], A.shininess);
    glUniform1f_(u[U_specular], A.specular);
    glUniform3fv_(u[U_fog_color], 1, A.fog_color);
    glUniform2f_(u[U_fog_range], A.fog_start, A.fog_end);
}
/* Draws the lines and billboards collected since the last flush. */
static void flush3(void) {
    if (!A.batch3_count)
        return;
    static const unsigned char no_tint[4] = {255, 255, 255, 255};
    float identity[16];
    m4_identity(identity);
    Draw3 draw = {identity, A.batch3_texture ? A.batch3_texture : white(), no_tint, false, false,
                  false, NULL};
    upload3(&draw); /* batched vertices are already in world space */
    glBindVertexArray_(A.vao3);
    glBindBuffer_(GL_ARRAY_BUFFER, A.vbo3);
    glBufferData_(GL_ARRAY_BUFFER, AP_BATCH_VERTICES * sizeof(Vertex3), NULL, GL_STREAM_DRAW);
    glBufferSubData_(GL_ARRAY_BUFFER, 0, A.batch3_count * sizeof(Vertex3), A.batch3);
    glDrawArrays_(A.batch3_mode, 0, A.batch3_count);
    A.batch3_count = 0;
}
static Vertex3 *reserve3(GLenum mode, GLuint texture, int count) {
    if (A.batch3_mode != mode || A.batch3_texture != texture ||
        A.batch3_count + count > AP_BATCH_VERTICES) {
        flush3();
        A.batch3_mode = mode;
        A.batch3_texture = texture;
    }
    Vertex3 *out = A.batch3 + A.batch3_count;
    A.batch3_count += count;
    return out;
}
/* Enters 3D drawing with a camera. A positive `fov` (degrees, vertical) is a perspective
   view; zero or less is an orthographic view `-fov` units tall. Ends with ap_end3d. */
void ap_begin3d(double eye_x, double eye_y, double eye_z, double target_x, double target_y,
                double target_z, double up_x, double up_y, double up_z, double fov, double near,
                double far) {
    if (!ready())
        return;
    if (A.shadow_pass) {
        fail("begin_3d inside begin_shadows; call end_shadows first");
        return;
    }
    flush();
    float eye[3] = {(float)eye_x, (float)eye_y, (float)eye_z};
    float target[3] = {(float)target_x, (float)target_y, (float)target_z};
    float up[3] = {(float)up_x, (float)up_y, (float)up_z};
    float aspect = A.logical_w / A.logical_h;
    if (near <= 0)
        near = 0.05;
    if (far <= near)
        far = near + 1000;
    if (fov > 0)
        m4_perspective(A.proj3, (float)fov, aspect, (float)near, (float)far);
    else {
        float half = (float)(fov < 0 ? -fov : 10) / 2;
        m4_ortho(A.proj3, -half * aspect, half * aspect, -half, half, (float)near, (float)far);
    }
    m4_look_at(A.view, eye, target, up);
    m4_multiply(A.viewproj, A.proj3, A.view);
    memcpy(A.eye, eye, sizeof eye);
    A.in3d = true;
    A.model_depth = 0;
    m4_identity(A.model[0]);
    glEnable_(GL_DEPTH_TEST);
    glDepthFunc_(GL_LEQUAL);
    glDepthMask_(GL_TRUE);
    glEnable_(GL_CULL_FACE);
    glCullFace_(GL_BACK);
    A.culling = A.depth_writing = true;
    glPolygonMode_(GL_FRONT_AND_BACK, A.wireframe ? GL_LINE : GL_FILL);
}
void ap_end3d(void) {
    if (!A.open || !A.in3d || A.shadow_pass)
        return;
    flush3();
    A.in3d = false;
    glDisable_(GL_DEPTH_TEST);
    glDisable_(GL_CULL_FACE);
    glPolygonMode_(GL_FRONT_AND_BACK, GL_FILL);
}
static bool in3d(void) {
    if (!ready())
        return false;
    if (!A.in3d)
        fail("3D drawing needs a camera; call begin_3d first");
    return A.in3d;
}
void ap_push3d(void) {
    if (A.model_depth + 1 >= AP_STACK_DEPTH) {
        fail("too many nested transforms (%d)", AP_STACK_DEPTH);
        return;
    }
    memcpy(A.model[A.model_depth + 1], A.model[A.model_depth], sizeof A.model[0]);
    A.model_depth++;
}
void ap_pop3d(void) {
    if (A.model_depth > 0)
        A.model_depth--;
}
void ap_translate3d(double x, double y, double z) {
    m4_translate(A.model[A.model_depth], (float)x, (float)y, (float)z);
}
void ap_rotate3d(double degrees, double x, double y, double z) {
    m4_rotate(A.model[A.model_depth], (float)degrees, (float)x, (float)y, (float)z);
}
void ap_scale3d(double x, double y, double z) {
    m4_scale(A.model[A.model_depth], (float)x, (float)y, (float)z);
}
void ap_ambient(long r, long g, long b) {
    A.ambient[0] = channel(r) / 255.0f, A.ambient[1] = channel(g) / 255.0f;
    A.ambient[2] = channel(b) / 255.0f;
}
/* The directional light: the direction it shines in, and its color. */
void ap_sun(double x, double y, double z, long r, long g, long b) {
    A.sun_direction[0] = (float)x, A.sun_direction[1] = (float)y, A.sun_direction[2] = (float)z;
    A.sun_color[0] = channel(r) / 255.0f, A.sun_color[1] = channel(g) / 255.0f;
    A.sun_color[2] = channel(b) / 255.0f;
}
/* Point light `index` (0..7), fading to nothing at `range`. A zero range turns it off. */
void ap_light(long index, double x, double y, double z, long r, long g, long b, double range) {
    if (index < 0 || index >= AP_MAX_LIGHTS) {
        fail("light index must be between 0 and %d", AP_MAX_LIGHTS - 1);
        return;
    }
    A.light_on[index] = range > 0;
    A.light_position[index][0] = (float)x, A.light_position[index][1] = (float)y;
    A.light_position[index][2] = (float)z, A.light_position[index][3] = (float)range;
    A.light_color[index][0] = channel(r) / 255.0f, A.light_color[index][1] = channel(g) / 255.0f;
    A.light_color[index][2] = channel(b) / 255.0f;
}
void ap_lighting(bool on) {
    A.lighting = on;
}
/* How strong (0..1) and how tight (higher is tighter) highlights are. */
void ap_shine(double strength, double shininess) {
    A.specular = (float)fmax(strength, 0);
    A.shininess = (float)fmax(shininess, 1);
}
/* Fog from `start` to `end` units away; end <= start turns it off. */
void ap_fog(long r, long g, long b, double start, double end) {
    A.fog_color[0] = channel(r) / 255.0f, A.fog_color[1] = channel(g) / 255.0f;
    A.fog_color[2] = channel(b) / 255.0f;
    A.fog_start = (float)start, A.fog_end = (float)end;
}
void ap_wireframe(bool on) {
    A.wireframe = on;
    if (A.open && A.in3d && !A.shadow_pass) {
        flush3();
        glPolygonMode_(GL_FRONT_AND_BACK, on ? GL_LINE : GL_FILL);
    }
}
/* Whether triangles facing away from the camera are skipped (on by default). */
void ap_cull(bool on) {
    if (!A.open || !A.in3d)
        return;
    flush3();
    A.culling = on;
    if (on)
        glEnable_(GL_CULL_FACE);
    else
        glDisable_(GL_CULL_FACE);
}
/* Whether drawing writes depth; turn off for transparent things drawn last. */
void ap_depth_write(bool on) {
    if (!A.open)
        return;
    flush3();
    A.depth_writing = on;
    glDepthMask_(on ? GL_TRUE : GL_FALSE);
}
void ap_line3d(double x1, double y1, double z1, double x2, double y2, double z2, long r, long g,
               long b, long a) {
    if (!in3d() || A.shadow_pass)
        return;
    AP_COLOR(color);
    Vertex3 *v = reserve3(GL_LINES, 0, 2);
    const float *m = A.model[A.model_depth];
    float ends[2][4] = {{(float)x1, (float)y1, (float)z1, 1}, {(float)x2, (float)y2, (float)z2, 1}};
    for (int i = 0; i < 2; i++) {
        float world[4];
        m4_transform(world, m, ends[i]);
        memset(&v[i], 0, sizeof v[i]);
        v[i].x = world[0], v[i].y = world[1], v[i].z = world[2];
        v[i].ny = 1;
        v[i].r = color[0], v[i].g = color[1], v[i].b = color[2], v[i].a = color[3];
    }
}
/* A square grid of lines on the ground plane, centered on the origin. */
void ap_grid(long cells, double spacing, long r, long g, long b, long a) {
    double half = cells * spacing / 2;
    for (long i = 0; i <= cells; i++) {
        double p = -half + i * spacing;
        ap_line3d(p, 0, -half, p, 0, half, r, g, b, a);
        ap_line3d(-half, 0, p, half, 0, p, r, g, b, a);
    }
}
/* A texture region facing the camera at a point in space, `width` by `height` units. */
void ap_billboard(long id, double sx, double sy, double sw, double sh, double x, double y,
                  double z, double width, double height, long r, long g, long b, long a) {
    if (!in3d() || A.shadow_pass)
        return;
    Texture *t = texture_of(id);
    if (!t) {
        fail("drawing a texture that is not loaded (or was freed)");
        return;
    }
    AP_COLOR(color);
    float center[4] = {(float)x, (float)y, (float)z, 1}, world[4];
    m4_transform(world, A.model[A.model_depth], center);
    float right[3] = {A.view[0], A.view[4], A.view[8]}, up[3] = {A.view[1], A.view[5], A.view[9]};
    float u0 = (float)(sx / t->width), v0 = (float)(sy / t->height);
    float u1 = (float)((sx + sw) / t->width), v1 = (float)((sy + sh) / t->height);
    if (t->flipped) {
        v0 = 1 - v0;
        v1 = 1 - v1;
    }
    float hw = (float)width / 2, hh = (float)height / 2;
    float corner[4][5] = {
        {-hw, hh, u0, v0, 0}, {hw, hh, u1, v0, 0}, {hw, -hh, u1, v1, 0}, {-hw, -hh, u0, v1, 0}};
    static const int order[6] = {0, 2, 1, 0, 3, 2};
    Vertex3 *v = reserve3(GL_TRIANGLES, t->id, 6);
    for (int i = 0; i < 6; i++) {
        const float *c = corner[order[i]];
        memset(&v[i], 0, sizeof v[i]);
        v[i].x = world[0] + right[0] * c[0] + up[0] * c[1];
        v[i].y = world[1] + right[1] * c[0] + up[1] * c[1];
        v[i].z = world[2] + right[2] * c[0] + up[2] * c[1];
        v[i].ny = 1;
        v[i].u = c[2], v[i].v = c[3];
        v[i].r = color[0], v[i].g = color[1], v[i].b = color[2], v[i].a = color[3];
    }
}
/* Where a point in space appears on screen: result 0 and 1 are x and y, result 2 is its
   depth (above 1, or negative, when it is outside the view). */
void ap_project(double x, double y, double z) {
    float point[4] = {(float)x, (float)y, (float)z, 1}, clip[4];
    m4_transform(clip, A.viewproj, point);
    if (fabsf(clip[3]) < 1e-9f) {
        A.result[0] = A.result[1] = 0;
        A.result[2] = -1;
        return;
    }
    A.result[0] = (clip[0] / clip[3] * 0.5f + 0.5f) * A.logical_w;
    A.result[1] = (1 - (clip[1] / clip[3] * 0.5f + 0.5f)) * A.logical_h;
    A.result[2] = clip[3] < 0 ? -1 : clip[2] / clip[3] * 0.5f + 0.5f;
}
/* The ray from the camera through a screen position: results 0..2 are its origin and 3..5
   its direction. */
void ap_screen_ray(double x, double y) {
    float inverse[16];
    if (!m4_invert(inverse, A.viewproj)) {
        memset(A.result, 0, sizeof A.result);
        return;
    }
    float nx = (float)(x / A.logical_w) * 2 - 1, ny = 1 - (float)(y / A.logical_h) * 2;
    float near[4] = {nx, ny, -1, 1}, far[4] = {nx, ny, 1, 1}, a[4], b[4];
    m4_transform(a, inverse, near);
    m4_transform(b, inverse, far);
    for (int i = 0; i < 3; i++) {
        a[i] /= a[3];
        b[i] /= b[3];
    }
    float direction[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    v3_normalize(direction);
    memcpy(A.result, a, 3 * sizeof(float));
    memcpy(A.result + 3, direction, 3 * sizeof(float));
}

/* --- Meshes -------------------------------------------------------------------------------- */
void ap_mesh_begin(void) {
    A.build_vertex_count = A.build_index_count = 0;
    A.build_tangents = false;
    A.build_color[0] = A.build_color[1] = A.build_color[2] = A.build_color[3] = 255;
}
void ap_mesh_color(long r, long g, long b, long a) {
    A.build_color[0] = channel(r), A.build_color[1] = channel(g);
    A.build_color[2] = channel(b), A.build_color[3] = channel(a);
}
/* Adds a vertex and returns its index. */
long ap_mesh_vertex(double x, double y, double z, double nx, double ny, double nz, double u,
                    double v) {
    if (A.build_vertex_count == A.build_vertex_capacity) {
        A.build_vertex_capacity = A.build_vertex_capacity ? A.build_vertex_capacity * 2 : 256;
        A.build_vertices = realloc(A.build_vertices, A.build_vertex_capacity * sizeof(Vertex3));
    }
    Vertex3 *out = &A.build_vertices[A.build_vertex_count];
    out->x = (float)x, out->y = (float)y, out->z = (float)z;
    out->nx = (float)nx, out->ny = (float)ny, out->nz = (float)nz;
    out->u = (float)u, out->v = (float)v;
    memcpy(&out->r, A.build_color, 4);
    out->tx = 1, out->ty = out->tz = 0, out->tw = 1;
    memset(out->joints, 0, sizeof out->joints);
    memset(out->weights, 0, sizeof out->weights);
    return A.build_vertex_count++;
}
/* Adds a triangle of three vertex indices, counterclockwise seen from its front. */
void ap_mesh_triangle(long a, long b, long c) {
    if (a < 0 || b < 0 || c < 0 || a >= A.build_vertex_count || b >= A.build_vertex_count ||
        c >= A.build_vertex_count) {
        fail("mesh triangle refers to a vertex that was not added");
        return;
    }
    if (A.build_index_count + 3 > A.build_index_capacity) {
        A.build_index_capacity = A.build_index_capacity ? A.build_index_capacity * 2 : 768;
        A.build_indices = realloc(A.build_indices, A.build_index_capacity * sizeof(unsigned));
    }
    A.build_indices[A.build_index_count++] = (unsigned)a;
    A.build_indices[A.build_index_count++] = (unsigned)b;
    A.build_indices[A.build_index_count++] = (unsigned)c;
}
/* Replaces every normal with the average of the faces around its vertex. */
void ap_mesh_smooth(void) {
    for (int i = 0; i < A.build_vertex_count; i++)
        A.build_vertices[i].nx = A.build_vertices[i].ny = A.build_vertices[i].nz = 0;
    for (int i = 0; i + 2 < A.build_index_count; i += 3) {
        Vertex3 *p = &A.build_vertices[A.build_indices[i]];
        Vertex3 *q = &A.build_vertices[A.build_indices[i + 1]];
        Vertex3 *s = &A.build_vertices[A.build_indices[i + 2]];
        float e1[3] = {q->x - p->x, q->y - p->y, q->z - p->z};
        float e2[3] = {s->x - p->x, s->y - p->y, s->z - p->z}, n[3];
        v3_cross(n, e1, e2);
        Vertex3 *corners[3] = {p, q, s};
        for (int k = 0; k < 3; k++) {
            corners[k]->nx += n[0];
            corners[k]->ny += n[1];
            corners[k]->nz += n[2];
        }
    }
    for (int i = 0; i < A.build_vertex_count; i++)
        v3_normalize(&A.build_vertices[i].nx);
}
/* Tangents along the texture's u direction, for normal maps (per vertex, from the triangles
   around it; w says whether the v direction is mirrored). */
static void compute_tangents(void) {
    int n = A.build_vertex_count;
    float *sum = calloc((size_t)n * 6, sizeof(float)); /* tangent then bitangent */
    for (int i = 0; i + 2 < A.build_index_count; i += 3) {
        unsigned k[3] = {A.build_indices[i], A.build_indices[i + 1], A.build_indices[i + 2]};
        Vertex3 *p = &A.build_vertices[k[0]], *q = &A.build_vertices[k[1]],
                *r = &A.build_vertices[k[2]];
        float e1[3] = {q->x - p->x, q->y - p->y, q->z - p->z};
        float e2[3] = {r->x - p->x, r->y - p->y, r->z - p->z};
        float du1 = q->u - p->u, dv1 = q->v - p->v, du2 = r->u - p->u, dv2 = r->v - p->v;
        float det = du1 * dv2 - du2 * dv1;
        if (fabsf(det) < 1e-12f)
            continue;
        float f = 1 / det;
        for (int c = 0; c < 3; c++) {
            float t = (e1[c] * dv2 - e2[c] * dv1) * f, b = (e2[c] * du1 - e1[c] * du2) * f;
            for (int j = 0; j < 3; j++) {
                sum[k[j] * 6 + c] += t;
                sum[k[j] * 6 + 3 + c] += b;
            }
        }
    }
    for (int i = 0; i < n; i++) {
        Vertex3 *v = &A.build_vertices[i];
        float *t = sum + i * 6, *b = t + 3, nn[3] = {v->nx, v->ny, v->nz};
        float d = nn[0] * t[0] + nn[1] * t[1] + nn[2] * t[2];
        float o[3] = {t[0] - nn[0] * d, t[1] - nn[1] * d, t[2] - nn[2] * d};
        if (o[0] * o[0] + o[1] * o[1] + o[2] * o[2] < 1e-20f) {
            /* no usable texture direction: any direction across the normal */
            float axis[3] = {fabsf(nn[0]) < 0.9f ? 1.0f : 0.0f, fabsf(nn[0]) < 0.9f ? 0.0f : 1.0f, 0};
            float c[3];
            v3_cross(c, axis, nn);
            v3_cross(o, nn, c);
        }
        v3_normalize(o);
        float c[3];
        v3_cross(c, nn, o);
        v->tx = o[0], v->ty = o[1], v->tz = o[2];
        v->tw = c[0] * b[0] + c[1] * b[1] + c[2] * b[2] < 0 ? -1.0f : 1.0f;
    }
    free(sum);
}
/* Finishes the mesh being built and returns its handle. */
long ap_mesh_end(void) {
    if (!ready())
        return 0;
    if (A.build_index_count < 3) {
        fail("a mesh needs at least one triangle");
        return 0;
    }
    int slot = 0;
    for (int i = 1; i < AP_MAX_MESHES && !slot; i++)
        if (!A.meshes[i].used)
            slot = i;
    if (!slot) {
        fail("too many meshes (%d)", AP_MAX_MESHES - 1);
        return 0;
    }
    if (!A.build_tangents)
        compute_tangents();
    flush3();
    Mesh *mesh = &A.meshes[slot];
    glGenVertexArrays_(1, &mesh->vao);
    glBindVertexArray_(mesh->vao);
    glGenBuffers_(1, &mesh->vbo);
    glBindBuffer_(GL_ARRAY_BUFFER, mesh->vbo);
    glBufferData_(GL_ARRAY_BUFFER, A.build_vertex_count * sizeof(Vertex3), A.build_vertices,
                  GL_STATIC_DRAW);
    glGenBuffers_(1, &mesh->ebo);
    glBindBuffer_(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    glBufferData_(GL_ELEMENT_ARRAY_BUFFER, A.build_index_count * sizeof(unsigned), A.build_indices,
                  GL_STATIC_DRAW);
    vertex3_layout();
    instance_layout();
    glBindVertexArray_(0);
    mesh->index_count = A.build_index_count;
    mesh->vertex_count = A.build_vertex_count;
    mesh->radius = 0;
    mesh->skinned = false;
    for (int c = 0; c < 3; c++) {
        mesh->low[c] = INFINITY;
        mesh->high[c] = -INFINITY;
    }
    for (int i = 0; i < A.build_vertex_count; i++) {
        Vertex3 *v = &A.build_vertices[i];
        float d = sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
        if (d > mesh->radius)
            mesh->radius = d;
        for (int c = 0; c < 3; c++) {
            mesh->low[c] = fminf(mesh->low[c], (&v->x)[c]);
            mesh->high[c] = fmaxf(mesh->high[c], (&v->x)[c]);
        }
        if (v->weights[0] + v->weights[1] + v->weights[2] + v->weights[3] > 0)
            mesh->skinned = true;
    }
    mesh->used = true;
    return slot;
}
static Mesh *mesh_of(long id) {
    return id > 0 && id < AP_MAX_MESHES && A.meshes[id].used ? &A.meshes[id] : NULL;
}
long ap_mesh_triangles(long id) {
    Mesh *mesh = mesh_of(id);
    return mesh ? mesh->index_count / 3 : 0;
}
double ap_mesh_radius(long id) {
    Mesh *mesh = mesh_of(id);
    return mesh ? mesh->radius : 0;
}
void ap_mesh_free(long id) {
    Mesh *mesh = mesh_of(id);
    if (!mesh)
        return;
    glDeleteVertexArrays_(1, &mesh->vao);
    glDeleteBuffers_(1, &mesh->vbo);
    glDeleteBuffers_(1, &mesh->ebo);
    memset(mesh, 0, sizeof *mesh);
}
/* A placement: translated, rotated by x, y, then z degrees, and scaled. */
static void placement(float *m, double x, double y, double z, double rx, double ry, double rz,
                      double sx, double sy, double sz) {
    m4_translate(m, (float)x, (float)y, (float)z);
    if (rz != 0)
        m4_rotate(m, (float)rz, 0, 0, 1);
    if (ry != 0)
        m4_rotate(m, (float)ry, 0, 1, 0);
    if (rx != 0)
        m4_rotate(m, (float)rx, 1, 0, 0);
    m4_scale(m, (float)sx, (float)sy, (float)sz);
}
static Material *material_of(long id) {
    return id > 0 && id < AP_MAX_MATERIALS && A.materials[id].used ? &A.materials[id] : NULL;
}
/* Draws `mesh` (`instances` times from the instance buffer when above 0), honoring the
   material's two-sidedness and transparency. */
static void draw_mesh(const Mesh *mesh, const Draw3 *d, int instances) {
    upload3(d);
    const Material *m = d->material;
    bool two_sided = m && m->double_sided && A.culling;
    bool see_through = m && m->blend && A.depth_writing && !A.shadow_pass;
    if (two_sided)
        glDisable_(GL_CULL_FACE);
    if (see_through)
        glDepthMask_(GL_FALSE);
    glBindVertexArray_(mesh->vao);
    if (instances > 0)
        glDrawElementsInstanced_(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT, (void *)0,
                                 instances);
    else
        glDrawElements_(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT, (void *)0);
    if (two_sided)
        glEnable_(GL_CULL_FACE);
    if (see_through)
        glDepthMask_(GL_TRUE);
}
/* Draws a mesh at a position, rotated by x, y, then z degrees, scaled, and tinted. Texture 0
   leaves it untextured; a material replaces the texture and lights it physically. The current
   3D transform applies first. */
void ap_mesh_draw(long id, long texture, long material, double x, double y, double z, double rx,
                  double ry, double rz, double sx, double sy, double sz, long r, long g, long b,
                  long a) {
    if (!in3d())
        return;
    Mesh *mesh = mesh_of(id);
    if (!mesh) {
        fail("drawing a mesh that does not exist (or was freed)");
        return;
    }
    flush3();
    AP_COLOR(color);
    float model[16];
    memcpy(model, A.model[A.model_depth], sizeof model);
    placement(model, x, y, z, rx, ry, rz, sx, sy, sz);
    Texture *t = texture_of(texture);
    Draw3 d = {model, t ? t->id : white(), color, true, false, false, material_of(material)};
    draw_mesh(mesh, &d, 0);
}
/* Fills the instance buffer from `count` column-major matrices of 16 doubles and, when
   `color_count` is `count`, colors of four 64-bit integers each (else every instance is white). */
static bool fill_instances(const double *matrices, long count, const long long *colors,
                           long color_count) {
    if (count <= 0)
        return false;
    if (count > 1000000) {
        fail("too many instances in one draw (%ld; at most 1000000)", count);
        return false;
    }
    if (count > A.instance_capacity) {
        while (A.instance_capacity < count)
            A.instance_capacity *= 2;
        A.instance_data =
            realloc(A.instance_data, (size_t)A.instance_capacity * 20 * sizeof(float));
        glBindBuffer_(GL_ARRAY_BUFFER, A.instance_vbo);
        glBufferData_(GL_ARRAY_BUFFER, (ptrdiff_t)A.instance_capacity * 20 * sizeof(float), NULL,
                      GL_STREAM_DRAW);
    }
    for (long i = 0; i < count; i++) {
        float *out = A.instance_data + i * 20;
        for (int k = 0; k < 16; k++)
            out[k] = (float)matrices[i * 16 + k];
        for (int k = 0; k < 4; k++)
            out[16 + k] = colors && color_count == count ? channel((long)colors[i * 4 + k]) / 255.0f
                                                         : 1.0f;
    }
    glBindBuffer_(GL_ARRAY_BUFFER, A.instance_vbo);
    glBufferData_(GL_ARRAY_BUFFER, (ptrdiff_t)A.instance_capacity * 20 * sizeof(float), NULL,
                  GL_STREAM_DRAW);
    glBufferSubData_(GL_ARRAY_BUFFER, 0, (ptrdiff_t)count * 20 * sizeof(float), A.instance_data);
    return true;
}
/* Draws a mesh once per matrix in one call: `matrices` holds `count` Mat4 values, `colors`
   `color_count` Color values (tinting each instance when there is one per matrix). */
void ap_mesh_draw_many(long id, long texture, long material, const void *matrices, long count,
                       const void *colors, long color_count, long r, long g, long b, long a) {
    if (!in3d())
        return;
    Mesh *mesh = mesh_of(id);
    if (!mesh) {
        fail("drawing a mesh that does not exist (or was freed)");
        return;
    }
    flush3();
    if (!matrices || !fill_instances((const double *)matrices, count, (const long long *)colors,
                                     color_count))
        return;
    AP_COLOR(color);
    float identity[16];
    m4_identity(identity);
    Texture *t = texture_of(texture);
    Draw3 d = {identity, t ? t->id : white(), color, true, true, false, material_of(material)};
    draw_mesh(mesh, &d, (int)count);
}

/* --- Materials ----------------------------------------------------------------------------- */
static float to_linear(float c) {
    return powf(c > 0 ? c : 0, 2.2f);
}
static int material_create(void) {
    int slot = 0;
    for (int i = 1; i < AP_MAX_MATERIALS && !slot; i++)
        if (!A.materials[i].used)
            slot = i;
    if (!slot) {
        fail("too many materials (%d)", AP_MAX_MATERIALS - 1);
        return 0;
    }
    Material *m = &A.materials[slot];
    memset(m, 0, sizeof *m);
    m->used = true;
    m->color[0] = m->color[1] = m->color[2] = m->color[3] = 1;
    m->metallic = 0;
    m->roughness = 0.5f;
    m->normal_scale = 1;
    m->occlusion_strength = 1;
    return slot;
}
/* A material: white, not metallic, half rough, with no textures. */
long ap_material_create(void) {
    return ready() ? material_create() : 0;
}
/* The base color (sRGB, 0 to 255 like every other color). */
void ap_material_color(long id, long r, long g, long b, long a) {
    Material *m = material_of(id);
    if (!m)
        return;
    m->color[0] = to_linear(channel(r) / 255.0f), m->color[1] = to_linear(channel(g) / 255.0f);
    m->color[2] = to_linear(channel(b) / 255.0f), m->color[3] = channel(a) / 255.0f;
}
/* How metallic (0 to 1) and how rough (0 polished to 1 matte) the surface is. */
void ap_material_surface(long id, double metallic, double roughness) {
    Material *m = material_of(id);
    if (!m)
        return;
    m->metallic = (float)fmin(fmax(metallic, 0), 1);
    m->roughness = (float)fmin(fmax(roughness, 0), 1);
}
/* Light the surface gives off by itself, `strength` times a color. */
void ap_material_emissive(long id, long r, long g, long b, double strength) {
    Material *m = material_of(id);
    if (!m)
        return;
    m->emissive[0] = to_linear(channel(r) / 255.0f) * (float)strength;
    m->emissive[1] = to_linear(channel(g) / 255.0f) * (float)strength;
    m->emissive[2] = to_linear(channel(b) / 255.0f) * (float)strength;
}
/* Texture `map`: 0 the base color, 1 the normal map, 2 metallic (blue) and roughness (green),
   3 emission, 4 ambient occlusion (red). Texture 0 removes it. */
void ap_material_map(long id, long map, long texture) {
    Material *m = material_of(id);
    if (!m || map < 0 || map >= MAP_COUNT)
        return;
    if (m->owned[map] && m->maps[map] != texture)
        ap_texture_free(m->maps[map]);
    m->maps[map] = texture_of(texture) ? (int)texture : 0;
    m->owned[map] = false;
}
/* How strongly the normal map bends the surface (1 as painted). */
void ap_material_normal_scale(long id, double scale) {
    Material *m = material_of(id);
    if (m)
        m->normal_scale = (float)scale;
}
/* Pixels less opaque than `cutoff` are not drawn (0 keeps them): leaves, fences, hair. */
void ap_material_cutoff(long id, double cutoff) {
    Material *m = material_of(id);
    if (m)
        m->alpha_cutoff = (float)fmin(fmax(cutoff, 0), 1);
}
/* Options: 1 two-sided, 2 transparent (blended, without hiding what is behind it), 4 unlit. */
void ap_material_options(long id, long options) {
    Material *m = material_of(id);
    if (!m)
        return;
    m->double_sided = options & 1;
    m->blend = options & 2;
    m->unlit = options & 4;
}
void ap_material_free(long id) {
    Material *m = material_of(id);
    if (!m)
        return;
    for (int i = 0; i < MAP_COUNT; i++)
        if (m->owned[i])
            ap_texture_free(m->maps[i]);
    memset(m, 0, sizeof *m);
}

/* --- Shadows ------------------------------------------------------------------------------- */
/* Shadows of the sun, through a `size` pixel square shadow map; `strength` 0 to 1 is how dark
   they are. */
void ap_shadows(bool on, long size, double strength) {
    if (!ready())
        return;
    if (A.shadow_pass) {
        fail("changing shadows inside begin_shadows");
        return;
    }
    A.shadow_strength = (float)fmin(fmax(strength, 0), 1);
    if (size < 64)
        size = 64;
    if (size > 8192)
        size = 8192;
    if (A.shadow_fbo && (!on || A.shadow_size != size)) {
        glDeleteFramebuffers_(1, &A.shadow_fbo);
        glDeleteTextures_(1, &A.shadow_depth);
        A.shadow_fbo = A.shadow_depth = 0;
    }
    A.shadows = on;
    A.shadow_ready = false;
    if (!on || A.shadow_fbo)
        return;
    flush();
    A.shadow_size = (int)size;
    glGenTextures_(1, &A.shadow_depth);
    glBindTexture_(GL_TEXTURE_2D, A.shadow_depth);
    glTexImage2D_(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, (int)size, (int)size, 0,
                  GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812D /* GL_CLAMP_TO_BORDER */);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812D);
    static const float border[4] = {1, 1, 1, 1};
    glTexParameterfv_(GL_TEXTURE_2D, 0x1004 /* GL_TEXTURE_BORDER_COLOR */, border);
    glTexParameteri_(GL_TEXTURE_2D, 0x884C /* GL_TEXTURE_COMPARE_MODE */,
                     0x884E /* GL_COMPARE_REF_TO_TEXTURE */);
    glTexParameteri_(GL_TEXTURE_2D, 0x884D /* GL_TEXTURE_COMPARE_FUNC */, GL_LEQUAL);
    glGenFramebuffers_(1, &A.shadow_fbo);
    glBindFramebuffer_(GL_FRAMEBUFFER, A.shadow_fbo);
    glFramebufferTexture2D_(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, A.shadow_depth, 0);
    glDrawBuffer_(GL_NONE);
    glReadBuffer_(GL_NONE);
    bool complete = glCheckFramebufferStatus_(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    set_target();
    if (!complete) {
        fail("the graphics driver rejected a %ldx%ld shadow map", size, size);
        ap_shadows(false, size, strength);
    }
}
/* Starts drawing the things that cast shadows, as seen from the sun, over a sphere around a
   point (everything outside it casts and receives no shadow). Ends with ap_shadow_end; 3D
   drawing afterwards in this frame is shadowed. */
void ap_shadow_begin(double x, double y, double z, double radius) {
    if (!ready())
        return;
    if (A.in3d) {
        fail(A.shadow_pass ? "begin_shadows twice; call end_shadows first"
                           : "begin_shadows inside begin_3d; call end_3d first");
        return;
    }
    if (!A.shadows || !A.shadow_fbo) {
        fail("shadows are off; call shadows(true) first");
        return;
    }
    flush();
    float r = (float)(radius > 0.01 ? radius : 0.01);
    float direction[3] = {A.sun_direction[0], A.sun_direction[1], A.sun_direction[2]};
    v3_normalize(direction);
    float center[3] = {(float)x, (float)y, (float)z};
    float eye[3] = {center[0] - direction[0] * r * 2, center[1] - direction[1] * r * 2,
                    center[2] - direction[2] * r * 2};
    float up[3] = {0, 1, 0};
    if (fabsf(direction[1]) > 0.99f)
        up[1] = 0, up[2] = 1;
    float view[16], projection[16];
    m4_look_at(view, eye, center, up);
    m4_ortho(projection, -r, r, -r, r, 0, r * 4);
    m4_multiply(A.shadow_viewproj, projection, view);
    /* Snap to whole shadow texels so the edges of shadows do not shimmer as the area moves. */
    float origin[4] = {0, 0, 0, 1}, clip[4];
    m4_transform(clip, A.shadow_viewproj, origin);
    float half = A.shadow_size / 2.0f;
    projection[12] += (roundf(clip[0] * half) - clip[0] * half) / half;
    projection[13] += (roundf(clip[1] * half) - clip[1] * half) / half;
    m4_multiply(A.shadow_viewproj, projection, view);
    memcpy(A.viewproj, A.shadow_viewproj, sizeof A.viewproj);
    memcpy(A.eye, eye, sizeof eye);
    A.shadow_offset = 2 * r / A.shadow_size * 1.5f;
    glBindFramebuffer_(GL_FRAMEBUFFER, A.shadow_fbo);
    glViewport_(0, 0, A.shadow_size, A.shadow_size);
    glDepthMask_(GL_TRUE);
    glClear_(GL_DEPTH_BUFFER_BIT);
    glEnable_(GL_DEPTH_TEST);
    glDepthFunc_(GL_LEQUAL);
    glDisable_(GL_CULL_FACE); /* thin and open shapes cast shadows from both sides */
    A.culling = false;
    A.depth_writing = true;
    glEnable_(0x8037 /* GL_POLYGON_OFFSET_FILL */);
    glPolygonOffset_(1.5f, 3.0f);
    glPolygonMode_(GL_FRONT_AND_BACK, GL_FILL);
    A.in3d = A.shadow_pass = true;
    A.model_depth = 0;
    m4_identity(A.model[0]);
}
void ap_shadow_end(void) {
    if (!A.open || !A.shadow_pass)
        return;
    A.in3d = A.shadow_pass = false;
    A.shadow_ready = true;
    glDisable_(0x8037);
    glDisable_(GL_DEPTH_TEST);
    set_target();
}
bool ap_shadows_on(void) {
    return A.shadows;
}
/* Whether draws are going into the shadow map now (so a scene can skip what casts none). */
bool ap_shadow_casting(void) {
    return A.shadow_pass;
}

/* Built-in shapes, centered on the origin. */
static void face(const float *origin, const float *across, const float *up, const float *normal,
                 float tiles_u, float tiles_v) {
    long base = A.build_vertex_count;
    for (int i = 0; i < 4; i++) {
        float s = (i == 1 || i == 2) ? 1.0f : 0.0f, t = i >= 2 ? 1.0f : 0.0f;
        ap_mesh_vertex(origin[0] + across[0] * s + up[0] * t, origin[1] + across[1] * s + up[1] * t,
                       origin[2] + across[2] * s + up[2] * t, normal[0], normal[1], normal[2],
                       s * tiles_u, (1 - t) * tiles_v);
    }
    ap_mesh_triangle(base, base + 1, base + 2);
    ap_mesh_triangle(base, base + 2, base + 3);
}
long ap_mesh_cube(double width, double height, double depth) {
    float w = (float)width / 2, h = (float)height / 2, d = (float)depth / 2;
    ap_mesh_begin();
    float faces[6][12] = {
        {-w, -h, d, 2 * w, 0, 0, 0, 2 * h, 0, 0, 0, 1},    /* front (+z) */
        {w, -h, -d, -2 * w, 0, 0, 0, 2 * h, 0, 0, 0, -1},  /* back */
        {w, -h, d, 0, 0, -2 * d, 0, 2 * h, 0, 1, 0, 0},    /* right */
        {-w, -h, -d, 0, 0, 2 * d, 0, 2 * h, 0, -1, 0, 0},  /* left */
        {-w, h, d, 2 * w, 0, 0, 0, 0, -2 * d, 0, 1, 0},    /* top */
        {-w, -h, -d, 2 * w, 0, 0, 0, 0, 2 * d, 0, -1, 0}}; /* bottom */
    for (int i = 0; i < 6; i++)
        face(faces[i], faces[i] + 3, faces[i] + 6, faces[i] + 9, 1, 1);
    return ap_mesh_end();
}
/* A flat rectangle on the ground facing up; the texture repeats `tiles` times across it. */
long ap_mesh_plane(double width, double depth, double tiles) {
    float w = (float)width / 2, d = (float)depth / 2;
    float origin[3] = {-w, 0, d}, across[3] = {2 * w, 0, 0}, up[3] = {0, 0, -2 * d};
    float normal[3] = {0, 1, 0};
    ap_mesh_begin();
    face(origin, across, up, normal, (float)tiles, (float)tiles);
    return ap_mesh_end();
}
long ap_mesh_sphere(double radius, long rings, long slices) {
    if (rings < 3)
        rings = 3;
    if (slices < 3)
        slices = 3;
    ap_mesh_begin();
    for (long ring = 0; ring <= rings; ring++) {
        double phi = M_PI * ring / rings, y = cos(phi), band = sin(phi);
        for (long slice = 0; slice <= slices; slice++) {
            double theta = 2 * M_PI * slice / slices;
            double x = band * cos(theta), z = -band * sin(theta);
            ap_mesh_vertex(x * radius, y * radius, z * radius, x, y, z, (double)slice / slices,
                           (double)ring / rings);
        }
    }
    for (long ring = 0; ring < rings; ring++)
        for (long slice = 0; slice < slices; slice++) {
            long a = ring * (slices + 1) + slice, b = a + slices + 1;
            if (ring != 0)
                ap_mesh_triangle(a, b, a + 1);
            if (ring != rings - 1)
                ap_mesh_triangle(a + 1, b, b + 1);
        }
    return ap_mesh_end();
}
/* A cylinder standing on the y axis; a different top radius makes a cone or a frustum. */
long ap_mesh_cylinder(double bottom_radius, double top_radius, double height, long slices) {
    if (slices < 3)
        slices = 3;
    double h = height / 2, slope = (bottom_radius - top_radius) / (height != 0 ? height : 1);
    ap_mesh_begin();
    for (long slice = 0; slice <= slices; slice++) {
        double theta = 2 * M_PI * slice / slices, x = cos(theta), z = -sin(theta);
        double length = sqrt(1 + slope * slope);
        ap_mesh_vertex(x * bottom_radius, -h, z * bottom_radius, x / length, slope / length,
                       z / length, (double)slice / slices, 1);
        ap_mesh_vertex(x * top_radius, h, z * top_radius, x / length, slope / length, z / length,
                       (double)slice / slices, 0);
    }
    for (long slice = 0; slice < slices; slice++) {
        long a = slice * 2;
        ap_mesh_triangle(a, a + 2, a + 1);
        ap_mesh_triangle(a + 1, a + 2, a + 3);
    }
    for (int cap = 0; cap < 2; cap++) {
        double radius = cap ? top_radius : bottom_radius, y = cap ? h : -h, ny = cap ? 1 : -1;
        if (radius <= 0)
            continue;
        long center = ap_mesh_vertex(0, y, 0, 0, ny, 0, 0.5, 0.5);
        for (long slice = 0; slice <= slices; slice++) {
            double theta = 2 * M_PI * slice / slices, x = cos(theta), z = -sin(theta);
            ap_mesh_vertex(x * radius, y, z * radius, 0, ny, 0, x / 2 + 0.5, z / 2 + 0.5);
        }
        for (long slice = 0; slice < slices; slice++) {
            if (cap)
                ap_mesh_triangle(center, center + 1 + slice, center + 2 + slice);
            else
                ap_mesh_triangle(center, center + 2 + slice, center + 1 + slice);
        }
    }
    return ap_mesh_end();
}
/* A ring around the y axis: `radius` to the middle of the tube, `tube` the tube's radius. */
long ap_mesh_torus(double radius, double tube, long rings, long sides) {
    if (rings < 3)
        rings = 3;
    if (sides < 3)
        sides = 3;
    ap_mesh_begin();
    for (long ring = 0; ring <= rings; ring++) {
        double theta = 2 * M_PI * ring / rings, cx = cos(theta), cz = -sin(theta);
        for (long side = 0; side <= sides; side++) {
            double phi = 2 * M_PI * side / sides, nx = cos(phi) * cx, ny = sin(phi),
                   nz = cos(phi) * cz;
            ap_mesh_vertex(cx * radius + nx * tube, ny * tube, cz * radius + nz * tube, nx, ny, nz,
                           (double)ring / rings, (double)side / sides);
        }
    }
    for (long ring = 0; ring < rings; ring++)
        for (long side = 0; side < sides; side++) {
            long a = ring * (sides + 1) + side, b = a + sides + 1;
            ap_mesh_triangle(a, b, a + 1);
            ap_mesh_triangle(a + 1, b, b + 1);
        }
    return ap_mesh_end();
}
/* Loads a Wavefront OBJ file: positions, texture coordinates, normals, and faces of any
   number of corners. Normals are computed when the file has none. */
long ap_mesh_load(const char *path) {
    if (!ready())
        return 0;
    FILE *file = fopen(path, "rb");
    if (!file) {
        fail("cannot open mesh '%s'", path);
        return 0;
    }
    float *positions = NULL, *uvs = NULL, *normals = NULL;
    int position_count = 0, uv_count = 0, normal_count = 0;
    int position_capacity = 0, uv_capacity = 0, normal_capacity = 0;
    bool had_normals = true, bad = false;
    char line[1024];
    ap_mesh_begin();
#define AP_GROW(array, count, capacity, width)                                                     \
    if ((count) == (capacity)) {                                                                   \
        (capacity) = (capacity) ? (capacity) * 2 : 1024;                                           \
        (array) = realloc((array), (size_t)(capacity) * (width) * sizeof(float));                  \
    }
    while (fgets(line, sizeof line, file)) {
        if (line[0] == 'v' && line[1] == ' ') {
            AP_GROW(positions, position_count, position_capacity, 3);
            float *p = positions + position_count++ * 3;
            p[0] = p[1] = p[2] = 0;
            sscanf(line + 2, "%f %f %f", &p[0], &p[1], &p[2]);
        } else if (line[0] == 'v' && line[1] == 't') {
            AP_GROW(uvs, uv_count, uv_capacity, 2);
            float *p = uvs + uv_count++ * 2;
            p[0] = p[1] = 0;
            sscanf(line + 3, "%f %f", &p[0], &p[1]);
        } else if (line[0] == 'v' && line[1] == 'n') {
            AP_GROW(normals, normal_count, normal_capacity, 3);
            float *p = normals + normal_count++ * 3;
            p[0] = p[1] = p[2] = 0;
            sscanf(line + 3, "%f %f %f", &p[0], &p[1], &p[2]);
        } else if (line[0] == 'f' && line[1] == ' ') {
            long corners[64];
            int count = 0;
            char *cursor = line + 2;
            while (count < 64) {
                while (*cursor == ' ' || *cursor == '\t')
                    cursor++;
                if (!*cursor || *cursor == '\n' || *cursor == '\r')
                    break;
                long v = strtol(cursor, &cursor, 10), vt = 0, vn = 0;
                if (*cursor == '/') {
                    cursor++;
                    if (*cursor != '/')
                        vt = strtol(cursor, &cursor, 10);
                    if (*cursor == '/') {
                        cursor++;
                        vn = strtol(cursor, &cursor, 10);
                    }
                }
                if (v < 0)
                    v += position_count + 1;
                if (vt < 0)
                    vt += uv_count + 1;
                if (vn < 0)
                    vn += normal_count + 1;
                if (v < 1 || v > position_count || vt > uv_count || vn > normal_count) {
                    bad = true;
                    break;
                }
                const float *p = positions + (v - 1) * 3;
                const float *t = vt > 0 ? uvs + (vt - 1) * 2 : NULL;
                const float *n = vn > 0 ? normals + (vn - 1) * 3 : NULL;
                if (!n)
                    had_normals = false;
                corners[count++] = ap_mesh_vertex(p[0], p[1], p[2], n ? n[0] : 0, n ? n[1] : 0,
                                                  n ? n[2] : 0, t ? t[0] : 0, t ? 1 - t[1] : 0);
            }
            for (int i = 1; i + 1 < count; i++)
                ap_mesh_triangle(corners[0], corners[i], corners[i + 1]);
        }
    }
    fclose(file);
    free(positions);
    free(uvs);
    free(normals);
    if (bad || A.build_index_count < 3) {
        fail(bad ? "mesh '%s' has a face that refers to missing data"
                 : "mesh '%s' has no faces",
             path);
        return 0;
    }
    if (!had_normals)
        ap_mesh_smooth();
    return ap_mesh_end();
}

/* --- Models -------------------------------------------------------------------------------- */
#include "model.h"

static Model *model_of(long id) {
    return id > 0 && id < AP_MAX_MODELS ? A.models[id] : NULL;
}
static Model *model_or_fail(long id) {
    Model *model = model_of(id);
    if (!model)
        fail("using a model that is not loaded (or was freed)");
    return model;
}
/* Loads a glTF 2.0 model (.gltf or .glb) or an OBJ model with its MTL materials. */
long ap_model_load(const char *path) {
    if (!ready())
        return 0;
    int slot = 0;
    for (int i = 1; i < AP_MAX_MODELS && !slot; i++)
        if (!A.models[i])
            slot = i;
    if (!slot) {
        fail("too many models (%d)", AP_MAX_MODELS - 1);
        return 0;
    }
    size_t length = strlen(path);
    bool obj = length > 4 && SDL_strcasecmp(path + length - 4, ".obj") == 0;
    char error[512] = "";
    Model *model = obj ? load_obj(path, error, sizeof error) : load_gltf(path, error, sizeof error);
    if (!model) {
        fail("%s", error);
        return 0;
    }
    model_bounds(model);
    A.models[slot] = model;
    return slot;
}
void ap_model_free(long id) {
    Model *model = model_of(id);
    if (!model)
        return;
    if (A.in3d)
        flush3();
    model_destroy(model);
    A.models[id] = NULL;
}
long ap_model_triangles(long id) {
    Model *model = model_of(id);
    long total = 0;
    for (int i = 0; model && i < model->primitive_count; i++)
        total += ap_mesh_triangles(model->primitives[i].mesh);
    return total;
}
/* The box around the model as loaded: results 0 to 2 are its low corner, 3 to 5 its high. */
void ap_model_bounds(long id) {
    Model *model = model_or_fail(id);
    if (!model)
        return;
    memcpy(A.result, model->low, 3 * sizeof(float));
    memcpy(A.result + 3, model->high, 3 * sizeof(float));
}
long ap_model_material_count(long id) {
    Model *model = model_of(id);
    return model ? model->material_count : 0;
}
/* Material `index` of the model, to change its settings (it stays the model's). */
long ap_model_material(long id, long index) {
    Model *model = model_of(id);
    return model && index >= 0 && index < model->material_count ? model->materials[index] : 0;
}
long ap_model_clip_count(long id) {
    Model *model = model_of(id);
    return model ? model->clip_count : 0;
}
const char *ap_model_clip_name(long id, long clip) {
    Model *model = model_of(id);
    return model && clip >= 0 && clip < model->clip_count ? model->clips[clip].name : "";
}
/* The clip's glTF "extras" as JSON text ("" when it has none): where frame data can live. */
const char *ap_model_clip_extras(long id, long clip) {
    Model *model = model_of(id);
    return model && clip >= 0 && clip < model->clip_count && model->clips[clip].extras
               ? model->clips[clip].extras
               : "";
}
double ap_model_clip_length(long id, long clip) {
    Model *model = model_of(id);
    return model && clip >= 0 && clip < model->clip_count ? model->clips[clip].length : 0;
}
/* The clip with a name, or -1. */
long ap_model_clip_find(long id, const char *name) {
    Model *model = model_of(id);
    for (int i = 0; model && i < model->clip_count; i++)
        if (!strcmp(model->clips[i].name, name))
            return i;
    return -1;
}
long ap_model_node_count(long id) {
    Model *model = model_of(id);
    return model ? model->node_count : 0;
}
const char *ap_model_node_name(long id, long node) {
    Model *model = model_of(id);
    return model && node >= 0 && node < model->node_count ? model->nodes[node].name : "";
}
long ap_model_node_find(long id, const char *name) {
    Model *model = model_of(id);
    for (int i = 0; model && i < model->node_count; i++)
        if (!strcmp(model->nodes[i].name, name))
            return i;
    return -1;
}
/* Back to the pose the model was loaded in. */
void ap_model_reset(long id) {
    Model *model = model_or_fail(id);
    if (!model)
        return;
    for (int i = 0; i < model->node_count; i++) {
        Node *n = &model->nodes[i];
        memcpy(n->t, n->rest_t, sizeof n->t);
        memcpy(n->r, n->rest_r, sizeof n->r);
        memcpy(n->s, n->rest_s, sizeof n->s);
    }
    model->posed = false;
}
/* Poses the model as clip `clip` is at `time` seconds (wrapped around when `loop`), mixed into
   the current pose by `weight` (1 replaces it; less blends toward it, for crossfades). Nodes the
   clip does not move keep their pose. */
void ap_model_animate(long id, long clip, double time, double weight, bool loop) {
    Model *model = model_or_fail(id);
    if (!model)
        return;
    if (clip < 0 || clip >= model->clip_count) {
        fail("the model has no animation %ld (it has %d)", clip, model->clip_count);
        return;
    }
    Clip *c = &model->clips[clip];
    float t = (float)time;
    if (loop && c->length > 0) {
        t = fmodf(t, c->length);
        if (t < 0)
            t += c->length;
    }
    float w = (float)fmin(fmax(weight, 0), 1);
    if (w <= 0)
        return;
    for (int i = 0; i < c->channel_count; i++) {
        Channel *channel = &c->channels[i];
        Node *n = &model->nodes[channel->node];
        float value[4];
        sample(channel, t, value);
        float *target = channel->path == PATH_TRANSLATION ? n->t
                        : channel->path == PATH_ROTATION  ? n->r
                                                          : n->s;
        int width = channel->path == PATH_ROTATION ? 4 : 3;
        if (channel->path == PATH_ROTATION) {
            float dot = 0;
            for (int k = 0; k < 4; k++)
                dot += target[k] * value[k];
            if (dot < 0)
                for (int k = 0; k < 4; k++)
                    value[k] = -value[k];
        }
        for (int k = 0; k < width; k++)
            target[k] += (value[k] - target[k]) * w;
        if (channel->path == PATH_ROTATION)
            quat_normalize(target);
        n->has_matrix = false;
    }
    model->posed = false;
}
/* Sets a node's own translation, rotation (a quaternion), and scale, relative to its parent. */
void ap_model_node_set(long id, long node, double tx, double ty, double tz, double qx, double qy,
                       double qz, double qw, double sx, double sy, double sz) {
    Model *model = model_or_fail(id);
    if (!model)
        return;
    if (node < 0 || node >= model->node_count) {
        fail("the model has no node %ld", node);
        return;
    }
    Node *n = &model->nodes[node];
    n->t[0] = (float)tx, n->t[1] = (float)ty, n->t[2] = (float)tz;
    n->r[0] = (float)qx, n->r[1] = (float)qy, n->r[2] = (float)qz, n->r[3] = (float)qw;
    quat_normalize(n->r);
    n->s[0] = (float)sx, n->s[1] = (float)sy, n->s[2] = (float)sz;
    n->has_matrix = false;
    model->posed = false;
}
/* A node's own transform: results 0 to 2 translation, 3 to 6 rotation, 7 to 9 scale. */
void ap_model_node_local(long id, long node) {
    Model *model = model_or_fail(id);
    if (!model || node < 0 || node >= model->node_count)
        return;
    Node *n = &model->nodes[node];
    memcpy(A.result, n->t, 3 * sizeof(float));
    memcpy(A.result + 3, n->r, 4 * sizeof(float));
    memcpy(A.result + 7, n->s, 3 * sizeof(float));
}
/* A node's transform in the model's space, in the current pose: results 0 to 15, column-major. */
void ap_model_node_world(long id, long node) {
    Model *model = model_or_fail(id);
    if (!model)
        return;
    if (node < 0 || node >= model->node_count) {
        fail("the model has no node %ld", node);
        return;
    }
    model_pose(model);
    memcpy(A.result, model->nodes[node].world, 16 * sizeof(float));
}
/* Draws every primitive of the model: opaque ones first, then see-through ones. `base` is the
   placement (for instanced draws, what each instance's matrix applies to). */
static void draw_model(Model *model, const float *base, long material, const unsigned char *tint,
                       int instances) {
    model_pose(model);
    Material *override = material_of(material);
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < model->node_count; k++) {
            Node *n = &model->nodes[model->order[k]];
            if (n->mesh < 0)
                continue;
            ModelMesh *mesh = &model->meshes[n->mesh];
            bool skinned = n->skin >= 0 && model->skins[n->skin].count > 0;
            bool joints_ready = false;
            float matrix[16];
            if (skinned) /* skinned vertices are placed by their joints, not by the node */
                memcpy(matrix, base, sizeof matrix);
            else
                m4_multiply(matrix, base, n->world);
            for (int p = mesh->first; p < mesh->first + mesh->count; p++) {
                Primitive *primitive = &model->primitives[p];
                Material *m = override ? override : material_of(primitive->material);
                if ((m && m->blend && !A.shadow_pass) != (pass == 1))
                    continue;
                Mesh *gpu = mesh_of(primitive->mesh);
                if (!gpu)
                    continue;
                if (skinned && gpu->skinned && !joints_ready) {
                    Skin *skin = &model->skins[n->skin];
                    static float joints[AP_MAX_JOINTS * 16];
                    for (int j = 0; j < skin->count; j++)
                        m4_multiply(joints + j * 16, model->nodes[skin->joints[j]].world,
                                    skin->inverse_bind + j * 16);
                    glBindTexture_(GL_TEXTURE_2D, A.joint_texture);
                    glTexSubImage2D_(GL_TEXTURE_2D, 0, 0, 0, skin->count * 4, 1, GL_RGBA, GL_FLOAT,
                                     joints);
                    joints_ready = true;
                }
                Draw3 d = {matrix, white(), tint, true, instances > 0, skinned && gpu->skinned, m};
                draw_mesh(gpu, &d, instances);
            }
        }
}
/* Draws a model like ap_mesh_draw; material 0 keeps the model's own materials. */
void ap_model_draw(long id, long material, double x, double y, double z, double rx, double ry,
                   double rz, double sx, double sy, double sz, long r, long g, long b, long a) {
    if (!in3d())
        return;
    Model *model = model_or_fail(id);
    if (!model)
        return;
    flush3();
    AP_COLOR(color);
    float base[16];
    memcpy(base, A.model[A.model_depth], sizeof base);
    placement(base, x, y, z, rx, ry, rz, sx, sy, sz);
    draw_model(model, base, material, color, 0);
}
/* Draws a model once per matrix, like ap_mesh_draw_many (all in the same pose). */
void ap_model_draw_many(long id, long material, const void *matrices, long count,
                        const void *colors, long color_count, long r, long g, long b, long a) {
    if (!in3d())
        return;
    Model *model = model_or_fail(id);
    if (!model)
        return;
    flush3();
    if (!matrices || !fill_instances((const double *)matrices, count, (const long long *)colors,
                                     color_count))
        return;
    AP_COLOR(color);
    float identity[16];
    m4_identity(identity);
    draw_model(model, identity, material, color, (int)count);
}

/* --- Audio --------------------------------------------------------------------------------- */
/* The music track (see ap_music_play). */
static struct {
    SDL_AudioStream *stream;
    stb_vorbis *vorbis;      /* .ogg, read from the file as it plays */
    Uint8 *wav;              /* .wav, loaded whole */
    Uint32 wav_length, wav_at;
    SDL_AudioSpec spec;
    bool loop, ended, paused;
    float volume;
    Sint64 decoded;          /* sample frames decoded since the start of the track */
    Sint64 total;            /* sample frames in the track */
} M;
static bool audio_open(void) {
    if (A.audio)
        return true;
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
        return false;
    A.audio = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    return A.audio != 0;
}
static void audio_close(void) {
    ap_music_stop();
    for (int i = 0; i < AP_MAX_VOICES; i++)
        if (A.voices[i].stream)
            SDL_DestroyAudioStream(A.voices[i].stream);
    if (A.audio)
        SDL_CloseAudioDevice(A.audio);
    A.audio = 0;
}
/* Refills looping voices and frees the ones that finished. */
static void audio_update(void) {
    for (int i = 0; i < AP_MAX_VOICES; i++) {
        Voice *voice = &A.voices[i];
        if (!voice->stream)
            continue;
        Sound *sound = &A.sounds[voice->sound];
        int queued = SDL_GetAudioStreamQueued(voice->stream);
        if (voice->looping && sound->used) {
            if (queued < (int)sound->length)
                SDL_PutAudioStreamData(voice->stream, sound->data, (int)sound->length);
        } else if (queued <= 0 && SDL_GetAudioStreamAvailable(voice->stream) <= 0) {
            SDL_DestroyAudioStream(voice->stream);
            voice->stream = NULL;
        }
    }
}
static int sound_slot(void) {
    for (int i = 1; i < AP_MAX_SOUNDS; i++)
        if (!A.sounds[i].used)
            return i;
    fail("too many sounds (%d)", AP_MAX_SOUNDS - 1);
    return 0;
}
static bool ends_with(const char *text, const char *suffix) {
    size_t n = strlen(text), m = strlen(suffix);
    return n >= m && SDL_strcasecmp(text + n - m, suffix) == 0;
}
/* Decodes a whole .ogg file into 16-bit samples. */
static bool load_ogg(const char *path, Sound *sound) {
    int channels = 0, rate = 0;
    short *samples = NULL;
    int frames = stb_vorbis_decode_filename(path, &channels, &rate, &samples);
    if (frames < 0 || !samples) {
        SDL_SetError("not a readable Ogg Vorbis file");
        return false;
    }
    size_t bytes = (size_t)frames * (size_t)channels * sizeof(short);
    sound->data = SDL_malloc(bytes ? bytes : 1);
    memcpy(sound->data, samples, bytes);
    free(samples);
    sound->length = (Uint32)bytes;
    sound->spec.format = SDL_AUDIO_S16;
    sound->spec.channels = channels;
    sound->spec.freq = rate;
    return true;
}
/* Loads a WAV or Ogg Vorbis (.ogg) file. */
long ap_sound_load(const char *path) {
    if (!ready())
        return 0;
    int slot = sound_slot();
    if (!slot)
        return 0;
    Sound *sound = &A.sounds[slot];
    if (ends_with(path, ".ogg") ? !load_ogg(path, sound)
                                : !SDL_LoadWAV(path, &sound->spec, &sound->data, &sound->length)) {
        fail("cannot load sound '%s': %s", path, SDL_GetError());
        return 0;
    }
    sound->used = true;
    return slot;
}
/* Makes a sound: a tone of `frequency` Hz lasting `seconds`, sliding to `end_frequency`,
   fading out. Waves: 0 sine, 1 square, 2 sawtooth, 3 triangle, 4 noise. */
long ap_sound_tone(long wave, double frequency, double end_frequency, double seconds,
                   double volume) {
    if (!ready())
        return 0;
    if (seconds <= 0 || seconds > 60) {
        fail("a tone lasts between 0 and 60 seconds");
        return 0;
    }
    int slot = sound_slot();
    if (!slot)
        return 0;
    const int rate = 44100;
    int count = (int)(seconds * rate);
    if (count < 1)
        count = 1;
    Sint16 *samples = SDL_malloc((size_t)count * sizeof(Sint16));
    double phase = 0;
    unsigned noise = 0x1234567u;
    for (int i = 0; i < count; i++) {
        double t = (double)i / count, f = frequency + (end_frequency - frequency) * t, value;
        phase += f / rate;
        double cycle = phase - floor(phase);
        switch (wave) {
        case 1:
            value = cycle < 0.5 ? 1 : -1;
            break;
        case 2:
            value = cycle * 2 - 1;
            break;
        case 3:
            value = cycle < 0.5 ? cycle * 4 - 1 : 3 - cycle * 4;
            break;
        case 4:
            noise = noise * 1664525u + 1013904223u;
            value = (noise >> 8 & 0xFFFF) / 32767.5 - 1;
            break;
        default:
            value = sin(cycle * 2 * M_PI);
        }
        /* A short attack avoids a click; the rest fades out. */
        double envelope = fmin(i / (rate * 0.004), 1.0) * (1 - t);
        samples[i] = (Sint16)(value * envelope * fmin(fmax(volume, 0), 1) * 32000);
    }
    Sound *sound = &A.sounds[slot];
    sound->spec.format = SDL_AUDIO_S16;
    sound->spec.channels = 1;
    sound->spec.freq = rate;
    sound->data = (Uint8 *)samples;
    sound->length = (Uint32)count * sizeof(Sint16);
    sound->used = true;
    return slot;
}
double ap_sound_seconds(long id) {
    if (id <= 0 || id >= AP_MAX_SOUNDS || !A.sounds[id].used)
        return 0;
    Sound *sound = &A.sounds[id];
    int frame = SDL_AUDIO_FRAMESIZE(sound->spec);
    return frame > 0 && sound->spec.freq > 0
               ? (double)sound->length / frame / sound->spec.freq
               : 0;
}
/* Plays a sound and returns a voice number (0 when no voice is free or there is no audio
   device). `pitch` 1 is unchanged; 2 is an octave up. */
long ap_sound_play(long id, double volume, double pitch, bool loop) {
    if (!ready())
        return 0;
    if (id <= 0 || id >= AP_MAX_SOUNDS || !A.sounds[id].used) {
        fail("playing a sound that is not loaded (or was freed)");
        return 0;
    }
    if (!audio_open())
        return 0;
    Sound *sound = &A.sounds[id];
    for (int i = 0; i < AP_MAX_VOICES; i++) {
        Voice *voice = &A.voices[i];
        if (voice->stream)
            continue;
        voice->stream = SDL_CreateAudioStream(&sound->spec, NULL);
        if (!voice->stream || !SDL_BindAudioStream(A.audio, voice->stream)) {
            if (voice->stream)
                SDL_DestroyAudioStream(voice->stream);
            voice->stream = NULL;
            return 0;
        }
        SDL_SetAudioStreamGain(voice->stream, (float)(fmax(volume, 0) * A.master_volume));
        SDL_SetAudioStreamFrequencyRatio(voice->stream, (float)fmin(fmax(pitch, 0.05), 8));
        SDL_PutAudioStreamData(voice->stream, sound->data, (int)sound->length);
        if (!loop)
            SDL_FlushAudioStream(voice->stream);
        voice->sound = (int)id;
        voice->looping = loop;
        return i + 1;
    }
    return 0;
}
static Voice *voice_of(long voice) {
    return voice > 0 && voice <= AP_MAX_VOICES && A.voices[voice - 1].stream ? &A.voices[voice - 1]
                                                                             : NULL;
}
void ap_voice_stop(long id) {
    Voice *voice = voice_of(id);
    if (voice) {
        SDL_DestroyAudioStream(voice->stream);
        voice->stream = NULL;
    }
}
bool ap_voice_playing(long id) {
    return voice_of(id) != NULL;
}
void ap_voice_volume(long id, double volume) {
    Voice *voice = voice_of(id);
    if (voice)
        SDL_SetAudioStreamGain(voice->stream, (float)(fmax(volume, 0) * A.master_volume));
}
void ap_voice_pitch(long id, double pitch) {
    Voice *voice = voice_of(id);
    if (voice)
        SDL_SetAudioStreamFrequencyRatio(voice->stream, (float)fmin(fmax(pitch, 0.05), 8));
}
void ap_voice_pause(long id, bool paused) {
    Voice *voice = voice_of(id);
    if (!voice)
        return;
    if (paused)
        SDL_UnbindAudioStream(voice->stream);
    else
        SDL_BindAudioStream(A.audio, voice->stream);
}
void ap_stop_all(void) {
    for (int i = 1; i <= AP_MAX_VOICES; i++)
        ap_voice_stop(i);
}
/* The volume of everything, 0 to 1; applies to sounds played after the call. */
void ap_master_volume(double volume) {
    A.master_volume = (float)fmin(fmax(volume, 0), 1);
    if (M.stream)
        SDL_SetAudioStreamGain(M.stream, M.volume * A.master_volume);
}
bool ap_audio_available(void) {
    return A.open && audio_open();
}
/* --- Music: one track streamed from its file, decoded on the audio thread as it plays ------ */
static void SDLCALL music_feed(void *userdata, SDL_AudioStream *stream, int additional, int total) {
    (void)userdata;
    (void)total;
    short chunk[4096 * 2];
    int channels = M.spec.channels, frame = channels * (int)sizeof(short), empty_passes = 0;
    while (additional > 0 && !M.ended) {
        int got = 0;
        if (M.vorbis) {
            int want = (int)(sizeof chunk / sizeof chunk[0]) / channels * channels;
            got = stb_vorbis_get_samples_short_interleaved(M.vorbis, channels, chunk, want) * frame;
            if (got > 0)
                SDL_PutAudioStreamData(stream, chunk, got);
        } else if (M.wav) {
            Uint32 left = M.wav_length - M.wav_at;
            got = (int)(left < (Uint32)additional ? left : (Uint32)additional);
            if (got > 0)
                SDL_PutAudioStreamData(stream, M.wav + M.wav_at, got);
            M.wav_at += (Uint32)got;
        }
        if (got > 0) {
            M.decoded += got / frame;
            additional -= got;
            empty_passes = 0;
            continue;
        }
        if (!M.loop || ++empty_passes > 1) { /* the end (or an empty file) */
            M.ended = true;
            SDL_FlushAudioStream(stream);
            break;
        }
        if (M.vorbis)
            stb_vorbis_seek_start(M.vorbis);
        M.wav_at = 0;
        M.decoded = 0;
    }
}
void ap_music_stop(void) {
    if (M.stream)
        SDL_DestroyAudioStream(M.stream);
    if (M.vorbis)
        stb_vorbis_close(M.vorbis);
    SDL_free(M.wav);
    memset(&M, 0, sizeof M);
}
/* Starts streaming a .ogg or .wav file as the music, replacing any playing. */
bool ap_music_play(const char *path, double volume, bool loop) {
    ap_music_stop();
    if (!ready() || !audio_open())
        return false;
    if (ends_with(path, ".ogg")) {
        int error = 0;
        M.vorbis = stb_vorbis_open_filename(path, &error, NULL);
        if (!M.vorbis) {
            fail("cannot play music '%s': not a readable Ogg Vorbis file", path);
            return false;
        }
        stb_vorbis_info info = stb_vorbis_get_info(M.vorbis);
        M.spec.format = SDL_AUDIO_S16;
        M.spec.channels = info.channels;
        M.spec.freq = (int)info.sample_rate;
        M.total = stb_vorbis_stream_length_in_samples(M.vorbis);
    } else {
        if (!SDL_LoadWAV(path, &M.spec, &M.wav, &M.wav_length)) {
            fail("cannot play music '%s': %s", path, SDL_GetError());
            memset(&M, 0, sizeof M);
            return false;
        }
        M.total = M.wav_length / SDL_AUDIO_FRAMESIZE(M.spec);
    }
    M.loop = loop;
    M.volume = (float)fmax(volume, 0);
    M.stream = SDL_CreateAudioStream(&M.spec, NULL);
    if (!M.stream || !SDL_BindAudioStream(A.audio, M.stream)) {
        ap_music_stop();
        return false;
    }
    SDL_SetAudioStreamGain(M.stream, M.volume * A.master_volume);
    SDL_SetAudioStreamGetCallback(M.stream, music_feed, NULL);
    return true;
}
bool ap_music_playing(void) {
    if (!M.stream)
        return false;
    SDL_LockAudioStream(M.stream);
    bool playing = !M.ended || SDL_GetAudioStreamQueued(M.stream) > 0;
    SDL_UnlockAudioStream(M.stream);
    return playing;
}
void ap_music_pause(bool paused) {
    if (!M.stream || M.paused == paused)
        return;
    M.paused = paused;
    if (paused)
        SDL_UnbindAudioStream(M.stream);
    else
        SDL_BindAudioStream(A.audio, M.stream);
}
void ap_music_volume(double volume) {
    M.volume = (float)fmax(volume, 0);
    if (M.stream)
        SDL_SetAudioStreamGain(M.stream, M.volume * A.master_volume);
}
/* Seconds into the track that are being heard (decoded, less what waits in the stream). */
double ap_music_position(void) {
    if (!M.stream || M.spec.freq <= 0)
        return 0;
    SDL_LockAudioStream(M.stream);
    Sint64 heard = M.decoded - SDL_GetAudioStreamQueued(M.stream) / SDL_AUDIO_FRAMESIZE(M.spec);
    SDL_UnlockAudioStream(M.stream);
    if (heard < 0)
        heard += M.total; /* just looped */
    return (double)heard / M.spec.freq;
}
double ap_music_length(void) {
    return M.stream && M.spec.freq > 0 ? (double)M.total / M.spec.freq : 0;
}
void ap_music_seek(double seconds) {
    if (!M.stream)
        return;
    SDL_LockAudioStream(M.stream);
    Sint64 frame = (Sint64)(fmax(seconds, 0) * M.spec.freq);
    if (frame >= M.total)
        frame = M.total > 0 ? M.total - 1 : 0;
    if (M.vorbis)
        stb_vorbis_seek(M.vorbis, (unsigned)frame);
    else
        M.wav_at = (Uint32)(frame * SDL_AUDIO_FRAMESIZE(M.spec));
    M.decoded = frame;
    M.ended = false;
    SDL_ClearAudioStream(M.stream);
    SDL_UnlockAudioStream(M.stream);
}
void ap_sound_free(long id) {
    if (id <= 0 || id >= AP_MAX_SOUNDS || !A.sounds[id].used)
        return;
    for (int i = 0; i < AP_MAX_VOICES; i++)
        if (A.voices[i].stream && A.voices[i].sound == id) {
            SDL_DestroyAudioStream(A.voices[i].stream);
            A.voices[i].stream = NULL;
        }
    SDL_free(A.sounds[id].data);
    memset(&A.sounds[id], 0, sizeof A.sounds[id]);
}
