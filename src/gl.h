// The OpenGL entry points beyond 1.1 that the viewer needs, loaded by hand,
// and the shader program built from the two files in shaders\. This is the
// whole of the project's GL plumbing: Technical/Architecture.md records the
// decision to load the handful of functions ourselves rather than take a
// loader library, and Technical/Globe Viewer.md describes what the program
// they build draws.
//
// Deviation from standards/general.md §Modules (one namespace per header):
// the function pointers keep the GL API's own names at global scope, so
// the rendering code reads as ordinary GL calls (glUniform1f, not
// gl::glUniform1f). Only the loader and the program build are in gl::.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_RGBA32F 0x8814
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE3 0x84C3
#define GL_TEXTURE4 0x84C4
#define GL_TEXTURE5 0x84C5
#define GL_TEXTURE6 0x84C6
#define GL_TEXTURE7 0x84C7
#define GL_RG32F 0x8230
#define GL_RG 0x8227
#define GL_CLAMP_TO_EDGE 0x812F

typedef GLuint(APIENTRY* PFNGLCREATESHADERPROC)(GLenum);
typedef void(APIENTRY* PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void(APIENTRY* PFNGLCOMPILESHADERPROC)(GLuint);
typedef void(APIENTRY* PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
typedef void(APIENTRY* PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint(APIENTRY* PFNGLCREATEPROGRAMPROC)(void);
typedef void(APIENTRY* PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void(APIENTRY* PFNGLLINKPROGRAMPROC)(GLuint);
typedef void(APIENTRY* PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
typedef void(APIENTRY* PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef void(APIENTRY* PFNGLUSEPROGRAMPROC)(GLuint);
typedef GLint(APIENTRY* PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const GLchar*);
typedef void(APIENTRY* PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void(APIENTRY* PFNGLUNIFORM2FPROC)(GLint, GLfloat, GLfloat);
typedef void(APIENTRY* PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY* PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void(APIENTRY* PFNGLUNIFORM4FVPROC)(GLint, GLsizei, const GLfloat*);
typedef void(APIENTRY* PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void(APIENTRY* PFNGLUNIFORMMATRIX3FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void(APIENTRY* PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void(APIENTRY* PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint*);
typedef void(APIENTRY* PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef BOOL(APIENTRY* PFNWGLSWAPINTERVALEXTPROC)(int);

static PFNGLCREATESHADERPROC glCreateShader;
static PFNGLSHADERSOURCEPROC glShaderSource;
static PFNGLCOMPILESHADERPROC glCompileShader;
static PFNGLGETSHADERIVPROC glGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC glCreateProgram;
static PFNGLATTACHSHADERPROC glAttachShader;
static PFNGLLINKPROGRAMPROC glLinkProgram;
static PFNGLGETPROGRAMIVPROC glGetProgramiv;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
static PFNGLUSEPROGRAMPROC glUseProgram;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
static PFNGLUNIFORM1FPROC glUniform1f;
static PFNGLUNIFORM2FPROC glUniform2f;
static PFNGLUNIFORM3FPROC glUniform3f;
static PFNGLUNIFORM4FPROC glUniform4f;
static PFNGLUNIFORM4FVPROC glUniform4fv;
static PFNGLUNIFORM1IPROC glUniform1i;
static PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv;
static PFNGLACTIVETEXTUREPROC glActiveTexture;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
static PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;

namespace gl {

template <typename T> inline bool load(T& fn, const char* name) {
    fn = (T)wglGetProcAddress(name);
    if (!fn) fprintf(stderr, "missing GL function: %s\n", name);
    return fn != nullptr;
}

// Resolve every entry point above against the current context. False if
// one that is not optional is missing.
inline bool loadGL() {
    bool ok = true;
    ok &= load(glCreateShader, "glCreateShader");
    ok &= load(glShaderSource, "glShaderSource");
    ok &= load(glCompileShader, "glCompileShader");
    ok &= load(glGetShaderiv, "glGetShaderiv");
    ok &= load(glGetShaderInfoLog, "glGetShaderInfoLog");
    ok &= load(glCreateProgram, "glCreateProgram");
    ok &= load(glAttachShader, "glAttachShader");
    ok &= load(glLinkProgram, "glLinkProgram");
    ok &= load(glGetProgramiv, "glGetProgramiv");
    ok &= load(glGetProgramInfoLog, "glGetProgramInfoLog");
    ok &= load(glUseProgram, "glUseProgram");
    ok &= load(glGetUniformLocation, "glGetUniformLocation");
    ok &= load(glUniform1f, "glUniform1f");
    ok &= load(glUniform2f, "glUniform2f");
    ok &= load(glUniform3f, "glUniform3f");
    ok &= load(glUniform4f, "glUniform4f");
    ok &= load(glUniform4fv, "glUniform4fv");
    ok &= load(glUniform1i, "glUniform1i");
    ok &= load(glUniformMatrix3fv, "glUniformMatrix3fv");
    ok &= load(glActiveTexture, "glActiveTexture");
    ok &= load(glGenVertexArrays, "glGenVertexArrays");
    ok &= load(glBindVertexArray, "glBindVertexArray");
    load(wglSwapIntervalEXT, "wglSwapIntervalEXT"); // optional
    return ok;
}

inline std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        fprintf(stderr, "cannot read %s\n", path.c_str());
        return "";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline GLuint compile(GLenum type, const std::string& src, const char* label) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        fprintf(stderr, "%s compile error:\n%s\n", label, log);
        return 0;
    }
    return s;
}

// The globe program from globe.vert and globe.frag in shaderDir (with its
// trailing separator). 0 on any compile or link error, which is printed.
inline GLuint buildProgram(const std::string& shaderDir) {
    GLuint vs = compile(GL_VERTEX_SHADER, readFile(shaderDir + "globe.vert"), "vertex");
    GLuint fs = compile(GL_FRAGMENT_SHADER, readFile(shaderDir + "globe.frag"), "fragment");
    if (!vs || !fs) return 0;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(prog, sizeof log, nullptr, log);
        fprintf(stderr, "link error:\n%s\n", log);
        return 0;
    }
    return prog;
}

} // namespace gl
