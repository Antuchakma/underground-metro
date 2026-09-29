// Minimal OpenGL 3.3 function loader (no GLAD needed).
#pragma once
#define GLFW_INCLUDE_GLEXT
#include <GLFW/glfw3.h>

#define METRO_GL_FUNCS(X)                                               \
    X(PFNGLCREATESHADERPROC, glCreateShader)                            \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                            \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                          \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                              \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                    \
    X(PFNGLDELETESHADERPROC, glDeleteShader)                            \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                          \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                            \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                              \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                            \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                  \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                  \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                  \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                                  \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                                  \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                    \
    X(PFNGLGETUNIFORMBLOCKINDEXPROC, glGetUniformBlockIndex)            \
    X(PFNGLUNIFORMBLOCKBINDINGPROC, glUniformBlockBinding)              \
    X(PFNGLBINDBUFFERBASEPROC, glBindBufferBase)                        \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                      \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                      \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                          \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)              \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)      \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                      \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                      \
    X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)            \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)        \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                    \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                    \
    X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)              \
    X(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, glRenderbufferStorageMultisample) \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)      \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                      \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)

#define METRO_DECLARE(type, name) extern type name;
METRO_GL_FUNCS(METRO_DECLARE)
#undef METRO_DECLARE

// Loads all functions above. Call after making a context current.
bool loadGL();
