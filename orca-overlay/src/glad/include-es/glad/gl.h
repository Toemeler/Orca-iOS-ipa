/*
 * Orca-iOS-ipa: <glad/gl.h> replacement for the SLIC3R_OPENGL_ES build.
 *
 * Under SLIC3R_OPENGL_ES, src/libvgcode compiles its own glad for GLES2
 * (glad/src/gles2.c). Orca's src/glad/CMakeLists.txt builds src/gl.c
 * unconditionally, so both translation units define every glad_gl* symbol --
 * 260 duplicate symbols at link time (step-4 run 4). The ES build therefore
 * drops gl.c and points the ~50 GUI sources that include <glad/gl.h> here, so
 * their declarations match the implementation actually being linked.
 */
#ifndef ORCA_IOS_GLAD_GL_ES_SHIM_H
#define ORCA_IOS_GLAD_GL_ES_SHIM_H

#include <glad/gles2.h>

#include <dlfcn.h>

/*
 * OpenGLManager::init_gl consults desktop feature macros that glad's GLES
 * header does not generate. Framebuffer objects are core in GLES 2.0, so the
 * ARB branch is the correct one to take; S3TC is not available on Apple GPUs.
 */
#ifndef GLAD_GL_ARB_framebuffer_object
#define GLAD_GL_ARB_framebuffer_object 1
#endif
#ifndef GLAD_GL_EXT_framebuffer_object
#define GLAD_GL_EXT_framebuffer_object 0
#endif
#ifndef GLAD_GL_EXT_texture_compression_s3tc
#define GLAD_GL_EXT_texture_compression_s3tc 0
#endif

/*
 * glad's gladLoaderLoadGL() dlopen()s OpenGL.framework, which does not exist on
 * iOS. OpenGLES.framework is linked directly, so every GLES entry point is
 * already in the process image and can be resolved from the global namespace.
 */
#ifndef gladLoaderLoadGL
static inline GLADapiproc orca_ios_gles_getproc(const char *name)
{
    return (GLADapiproc) dlsym(RTLD_DEFAULT, name);
}
#define gladLoaderLoadGL() gladLoadGLES2(orca_ios_gles_getproc)
#endif

#endif /* ORCA_IOS_GLAD_GL_ES_SHIM_H */
