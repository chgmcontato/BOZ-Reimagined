#!/usr/bin/env python3
"""Generates src/gl_thunks.c: one pass-through thunk per plain GL entry point.

Type codes: v void, i int32, u uint32 (enum, bitfield, uint, boolean), f float,
p pointer, z GLsizeiptr/GLintptr (sign-extended to host width).
Functions that need translation (fixed point, strings, ES-only names) live in gl.c.
"""
import sys

SIGS = """
v glActiveShaderProgram uu
v glActiveTexture u
v glAlphaFunc uf
v glAttachShader uu
v glBeginQuery uu
v glBindAttribLocation uup
v glBindBuffer uu
v glBindBufferBase uuu
v glBindBufferRange uuuzz
v glBindImageTexture uuiuiuu
v glBindProgramPipeline u
v glBindRenderbuffer uu
v glBindSampler uu
v glBindTexture uu
v glBindTransformFeedback uu
v glBindVertexArray u
v glBindVertexBuffer uuzi
v glBlendColor ffff
v glBlendEquationSeparate uu
v glBlendFunc uu
v glBlendFuncSeparate uuuu
v glBufferData uzpu
u glCheckFramebufferStatus u
v glClear u
v glClearColor ffff
v glClearDepthf f
v glClearStencil i
v glClientActiveTexture u
v glColor4f ffff
v glColorMask uuuu
v glColorPointer iuip
v glCompileShader u
v glCompressedTexImage3D uiuiiiiip
v glCompressedTexSubImage3D uiiiiiiiuip
v glCopyTexImage2D uiuiiiii
v glCopyTexSubImage2D uiiiiiii
u glCreateProgram
u glCreateShader u
v glCullFace u
v glDeleteBuffers ip
v glDeleteFramebuffers ip
v glDeleteProgram u
v glDeleteProgramPipelines ip
v glDeleteQueries ip
v glDeleteRenderbuffers ip
v glDeleteSamplers ip
v glDeleteShader u
v glDeleteTextures ip
v glDeleteVertexArrays ip
v glDepthFunc u
v glDepthMask u
v glDepthRangef ff
v glDetachShader uu
v glDisable u
v glDisableClientState u
v glDisableVertexAttribArray u
v glDrawArrays uii
v glDrawBuffers ip
v glDrawElements uiup
v glEnable u
v glEnableClientState u
v glEnableVertexAttribArray u
v glEndQuery u
v glFinish
v glFogf uf
v glFogfv up
v glFramebufferParameteri uui
v glFramebufferRenderbuffer uuuu
v glFramebufferTexture2D uuuui
v glFramebufferTextureLayer uuuii
v glFrontFace u
v glGenBuffers ip
v glGenerateMipmap u
v glGenFramebuffers ip
v glGenProgramPipelines ip
v glGenQueries ip
v glGenRenderbuffers ip
v glGenTextures ip
v glGenVertexArrays ip
v glGetActiveUniform uuipppp
i glGetAttribLocation up
v glGetBooleanv up
u glGetError
v glGetFloatv up
v glGetFramebufferAttachmentParameteriv uuup
v glGetProgramInfoLog uipp
v glGetProgramiv uup
v glGetQueryiv uup
v glGetQueryObjectuiv uup
v glGetRenderbufferParameteriv uup
v glGetShaderInfoLog uipp
v glGetShaderiv uup
i glGetUniformLocation up
v glHint uu
u glIsFramebuffer u
u glIsQuery u
u glIsRenderbuffer u
v glLightf uuf
v glLightfv uup
v glLightModelf uf
v glLightModelfv up
v glLineWidth f
v glLinkProgram u
v glLoadIdentity
v glLoadMatrixf p
v glLogicOp u
v glMaterialf uuf
v glMaterialfv uup
v glMatrixMode u
v glMultiTexCoord4f uffff
v glMultMatrixf p
v glNormal3f fff
v glNormalPointer uip
v glPixelStorei ui
v glPointSize f
v glPolygonOffset ff
v glPopMatrix
v glPushMatrix
v glReadBuffer u
v glReadPixels iiiiuup
v glRenderbufferStorage uuii
v glRenderbufferStorageMultisample uiuii
v glRotatef ffff
v glSampleMaski uu
v glSamplerParameterfv uup
v glSamplerParameteriv uup
v glScalef fff
v glScissor iiii
v glShadeModel u
v glShaderBinary ipupi
v glStencilFunc uiu
v glStencilFuncSeparate uuiu
v glStencilMask u
v glStencilMaskSeparate uu
v glStencilOp uuu
v glStencilOpSeparate uuuu
v glTexCoordPointer iuip
v glTexEnvf uuf
v glTexEnvfv uup
v glTexEnvi uui
v glTexImage2D uiiiiiuup
v glTexImage3D uiiiiiiuup
v glTexParameterfv uup
v glTexParameteri uui
v glTexParameteriv uup
v glTexStorage2D uiuii
v glTexStorage2DMultisample uiuiiu
v glTexStorage3D uiuiii
v glTexSubImage2D uiiiiiuup
v glTexSubImage3D uiiiiiiiuup
v glTranslatef fff
v glUniform1f if
v glUniform1fv iip
v glUniform1i ii
v glUniform1iv iip
v glUniform1uiv iip
v glUniform2fv iip
v glUniform2iv iip
v glUniform2uiv iip
v glUniform3fv iip
v glUniform3iv iip
v glUniform3uiv iip
v glUniform4f iffff
v glUniform4fv iip
v glUniform4iv iip
v glUniform4uiv iip
v glUniformMatrix2fv iiup
v glUniformMatrix2x3fv iiup
v glUniformMatrix2x4fv iiup
v glUniformMatrix3fv iiup
v glUniformMatrix3x2fv iiup
v glUniformMatrix3x4fv iiup
v glUniformMatrix4fv iiup
v glUniformMatrix4x2fv iiup
v glUniformMatrix4x3fv iiup
v glUseProgram u
v glUseProgramStages uuu
v glVertexAttribBinding uu
v glVertexAttribDivisor uu
v glVertexAttribFormat uiuuu
v glVertexAttribIFormat uiuu
v glVertexAttribIPointer uiuip
v glVertexAttribPointer uiuuip
v glVertexBindingDivisor uu
v glVertexPointer iuip
v glViewport iiii
"""

CTYPE = {"i": "int32_t", "u": "uint32_t", "f": "float", "p": "void *", "z": "intptr_t"}
FETCH = {
    "i": "(int32_t)arg({n})",
    "u": "arg({n})",
    "f": "argf({n})",
    "p": "G2H(arg({n}))",
    "z": "(intptr_t)(int32_t)arg({n})",
}

CACHE_GUARDS = {
    "glBindBuffer": "gl_state_bind_buffer(arg(0), arg(1))",
    "glBindTexture": "gl_state_bind_texture(arg(0), arg(1))",
    "glUseProgram": "gl_state_use_program(arg(0))",
    "glActiveTexture": "gl_state_active_texture(arg(0))",
    "glVertexAttribPointer": "gl_state_attrib_pointer(arg(0), (int32_t)arg(1), arg(2), arg(3), (int32_t)arg(4), (uintptr_t)arg(5))",
}

# These alternate APIs change bindings/attribute descriptors behind the common
# ES path. Invalidate rather than guessing which VAO/binding they affect.
INVALIDATE = {
    "glDeleteVertexArrays", "glDeleteBuffers",
    "glBindBufferBase", "glBindBufferRange", "glBindVertexBuffer",
    "glVertexAttribBinding", "glVertexAttribFormat", "glVertexAttribIFormat",
    "glVertexAttribIPointer", "glUseProgramStages",
    # Compatibility drivers may alias conventional and generic array state.
    "glVertexPointer", "glColorPointer", "glNormalPointer", "glTexCoordPointer",
}

AFTER_CALL = {
    "glBindVertexArray": "gl_state_vertex_array(arg(0));",
    "glGenTextures": "gl_state_textures_created((int32_t)arg(0), G2H(arg(1)));",
    "glDeleteTextures": "gl_state_textures_deleted((int32_t)arg(0), G2H(arg(1)));",
    "glGetProgramiv": "gl_state_program_status(arg(0), arg(1), G2H(arg(2)));",
}


def main(out_path):
    lines = [
        "/* Generated by tools/gen_gl.py; do not edit. */",
        '#include "host.h"',
        '#include "gl_state.h"',
        "",
        "typedef void (*gl_any)(void);",
        "",
    ]
    names = []
    for row in SIGS.strip().splitlines():
        parts = row.split()
        rtype, name = parts[0], parts[1]
        args = parts[2] if len(parts) > 2 else ""
        names.append(name)
        proto = ", ".join(CTYPE[c] for c in args) or "void"
        rc = "void" if rtype == "v" else CTYPE[rtype]
        call_args = ", ".join(FETCH[c].format(n=i) for i, c in enumerate(args))
        lines.append("static void gl_%s(void) {" % name)
        lines.append("    static %s (*fn)(%s);" % (rc, proto))
        lines.append("    if (!fn) {")
        lines.append('        *(void **)&fn = gl_proc("%s");' % name)
        lines.append("    }")
        if name in CACHE_GUARDS:
            lines.append("    if (!%s) return;" % CACHE_GUARDS[name])
        if name in INVALIDATE:
            lines.append("    gl_state_invalidate();")
        if name in {"glDeleteProgram", "glLinkProgram"}:
            lines.append("    gl_state_program_changed(arg(0));")
        if rtype == "v":
            lines.append("    fn(%s);" % call_args)
            if name in AFTER_CALL:
                lines.append("    %s" % AFTER_CALL[name])
        elif name == "glGetError":
            lines.append("    uint32_t error = fn();")
            lines.append("    if (error) gl_state_invalidate();")
            lines.append("    ret(error);")
        else:
            lines.append("    ret((uint32_t)fn(%s));" % call_args)
        lines.append("}")
        lines.append("")
    lines.append("static const char *const k_names[] = {")
    for name in names:
        lines.append('    "%s",' % name)
    lines.append("};")
    lines.append("")
    lines.append("int gl_thunk_known(const char *name) {")
    lines.append("    for (size_t i = 0; i < sizeof(k_names) / sizeof(k_names[0]); ++i) {")
    lines.append("        if (strcmp(k_names[i], name) == 0) {")
    lines.append("            return 1;")
    lines.append("        }")
    lines.append("    }")
    lines.append("    return 0;")
    lines.append("}")
    lines.append("")
    lines.append("void register_gl_thunks(void) {")
    for name in names:
        lines.append('    import_register("%s", gl_%s);' % (name, name))
    lines.append("}")
    with open(out_path, "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main(sys.argv[1])
