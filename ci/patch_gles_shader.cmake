# Patch the Fast3D OpenGL shader before it is packed into soh.o2r.
# GLES fragment shaders otherwise default to mediump, which can lose
# texture-coordinate and texture-size precision on mobile GPUs. In addition,
# some GLES drivers evaluate GLSL mod() imprecisely in the WRAP macro; the
# resulting out-of-range texel can be clamped to alpha 0, making textured
# surfaces (including terrain) disappear. Use the upstream-safe clamp form.
if(NOT DEFINED SHADER_FILE OR NOT EXISTS "${SHADER_FILE}")
    message(FATAL_ERROR "SHADER_FILE must name an existing Fast3D GLSL template")
endif()

file(READ "${SHADER_FILE}" shader)

# Add explicit high precision only to the GLES variant. Keep this independent
# from the WRAP fix so repeated runs and already-precision-patched shaders work.
string(FIND "${shader}" "precision highp float;" precision_position)
if(precision_position EQUAL -1)
    set(shader_version_token "@{GLSL_VERSION}")
    string(FIND "${shader}" "${shader_version_token}" token_position)
    if(token_position EQUAL -1)
        message(FATAL_ERROR "Fast3D GLSL template is missing ${shader_version_token}")
    endif()

    set(shader_version_with_gles_precision [=[@{GLSL_VERSION}

@if(opengles)
    precision highp float;
    precision highp int;
@end]=])
    string(REPLACE "${shader_version_token}" "${shader_version_with_gles_precision}" patched_shader "${shader}")
    if(patched_shader STREQUAL shader)
        message(FATAL_ERROR "Failed to add GLES precision defaults to ${SHADER_FILE}")
    endif()
    set(shader "${patched_shader}")
endif()

# Match the shader fix from libultraship PR #1096. For the combiner output,
# clamping is equivalent to the old wrap-then-clamp path, but does not rely on
# driver-specific behavior of GLSL mod() near its discontinuity.
set(wrap_macro "WRAP(x, low, high) mod((x)-(low), (high)-(low)) + (low)")
set(wrap_macro_fixed "WRAP(x, low, high) clamp((x), (low), (high))")
string(FIND "${shader}" "${wrap_macro}" wrap_position)
if(NOT wrap_position EQUAL -1)
    string(REPLACE "${wrap_macro}" "${wrap_macro_fixed}" patched_shader "${shader}")
    if(patched_shader STREQUAL shader)
        message(FATAL_ERROR "Failed to replace the Fast3D WRAP macro in ${SHADER_FILE}")
    endif()
    set(shader "${patched_shader}")
else()
    string(FIND "${shader}" "${wrap_macro_fixed}" wrap_fixed_position)
    if(wrap_fixed_position EQUAL -1)
        message(FATAL_ERROR "Fast3D GLSL template has an unexpected WRAP macro: ${SHADER_FILE}")
    endif()
endif()

file(WRITE "${SHADER_FILE}" "${shader}")
