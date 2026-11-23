# Normalize Android ARM64 heap pointers before libultraship's OTR path-address
# checks. Android may tag heap pointers in their top byte; strip that tag only
# for range classification and keep the original pointer for all memory reads.
if(NOT DEFINED INTERPRETER_FILE OR NOT EXISTS "${INTERPRETER_FILE}")
    message(FATAL_ERROR "INTERPRETER_FILE must name libultraship/src/fast/interpreter.cpp")
endif()

file(READ "${INTERPRETER_FILE}" interpreter)
set(patched_interpreter "${interpreter}")

# The dispatch guard and signature check both classify OTR string pointers.
# Keep the TBI/MTE tag on the pointer passed to handlers and signature reads;
# only use the normalized value for rejecting invalid address ranges.
set(helper_marker "static constexpr uintptr_t gfx_address_for_range_check")
string(FIND "${patched_interpreter}" "${helper_marker}" helper_position)
if(helper_position EQUAL -1)
    set(include_anchor "#include <stdio.h>\n")
    string(FIND "${patched_interpreter}" "${include_anchor}" include_position)
    if(include_position EQUAL -1)
        message(FATAL_ERROR "Could not find the include anchor in ${INTERPRETER_FILE}")
    endif()

    set(helper_code [=[

// Android arm64 may tag heap pointers in their top byte. Ignore that tag only
// for address-range classification; retain the original pointer for all reads
// so tagged-memory access semantics and resource-cache identity stay intact.
static constexpr uintptr_t gfx_address_for_range_check(uintptr_t address) {
#if defined(__ANDROID__) && defined(__aarch64__)
    return address & UINT64_C(0x00FFFFFFFFFFFFFF);
#else
    return address;
#endif
}

#if defined(__ANDROID__) && defined(__aarch64__)
static_assert(gfx_address_for_range_check(UINT64_C(0xB400007123456780)) == UINT64_C(0x0000007123456780));
static_assert(gfx_address_for_range_check(UINT64_C(0x0000007123456780)) == UINT64_C(0x0000007123456780));
static_assert(gfx_address_for_range_check(UINT64_C(0xFFFFFFFFFFFFFFFF)) > UINT64_C(0x0000FFFFFFFFFFFF));
static_assert(gfx_address_for_range_check(UINT64_C(0xB400000000000000)) < 0x10000);
#endif
]=])
    string(REPLACE "${include_anchor}" "${include_anchor}${helper_code}" with_helper "${patched_interpreter}")
    if(with_helper STREQUAL patched_interpreter)
        message(FATAL_ERROR "Failed to add Android ARM64 tagged-pointer range helper to ${INTERPRETER_FILE}")
    endif()
    set(patched_interpreter "${with_helper}")
endif()

set(old_dispatch "uintptr_t w1 = (uintptr_t)cmd->words.w1;")
set(new_dispatch "uintptr_t w1 = gfx_address_for_range_check((uintptr_t)cmd->words.w1);")
string(FIND "${patched_interpreter}" "${old_dispatch}" dispatch_position)
if(NOT dispatch_position EQUAL -1)
    string(REPLACE "${old_dispatch}" "${new_dispatch}" with_dispatch "${patched_interpreter}")
    if(with_dispatch STREQUAL patched_interpreter)
        message(FATAL_ERROR "Failed to normalize Android OTR dispatch-pointer checks in ${INTERPRETER_FILE}")
    endif()
    set(patched_interpreter "${with_dispatch}")
else()
    string(FIND "${patched_interpreter}" "${new_dispatch}" dispatch_position)
    if(dispatch_position EQUAL -1)
        message(FATAL_ERROR "Could not find the expected OTR dispatch-pointer check in ${INTERPRETER_FILE}")
    endif()
endif()

# Migrate the earlier patch, which stripped the tag from imgData itself.
# Keep only the range check normalized so memory-tagged reads use the original pointer.
set(old_signature_normalization [=[#if defined(__ANDROID__) && defined(__aarch64__)
    // Android ARM64 tagged-pointer normalization: heap pointers may use the top byte as a memory tag.
    // Strip it before the canonical-address filter and signature read below.
    i &= 0x00FFFFFFFFFFFFFFull;
    imgData = reinterpret_cast<const char*>(i);
#endif]=])
string(FIND "${patched_interpreter}" "${old_signature_normalization}" old_normalization_position)
if(NOT old_normalization_position EQUAL -1)
    string(REPLACE "${old_signature_normalization}" "" without_old_normalization "${patched_interpreter}")
    set(patched_interpreter "${without_old_normalization}")
endif()

set(old_signature "uintptr_t i = (uintptr_t)(imgData);")
set(new_signature "uintptr_t i = gfx_address_for_range_check((uintptr_t)imgData);")
string(FIND "${patched_interpreter}" "${old_signature}" signature_position)
if(NOT signature_position EQUAL -1)
    string(REPLACE "${old_signature}" "${new_signature}" with_signature "${patched_interpreter}")
    if(with_signature STREQUAL patched_interpreter)
        message(FATAL_ERROR "Failed to normalize Android OTR signature-pointer checks in ${INTERPRETER_FILE}")
    endif()
    set(patched_interpreter "${with_signature}")
else()
    string(FIND "${patched_interpreter}" "${new_signature}" signature_position)
    if(signature_position EQUAL -1)
        message(FATAL_ERROR "Could not find the expected OTR signature-pointer check in ${INTERPRETER_FILE}")
    endif()
endif()

if(NOT patched_interpreter STREQUAL interpreter)
    file(WRITE "${INTERPRETER_FILE}" "${patched_interpreter}")
endif()
