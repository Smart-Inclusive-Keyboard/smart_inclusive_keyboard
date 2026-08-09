if(CONFIG_BOARD_HAS_SPEAKER AND CONFIG_NARRATOR_ENABLE)
    # WAV assets live under wav/ and are embedded only when the
    # narrator is enabled. To populate them, drop the upstream
    # clackups/smart-keyboard audio/*.wav files into
    # components/narrator/wav/ -- see assets/README.md for the
    # full filename list.
    file(GLOB _wavs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "wav/*.wav")
    # Language-specific clips are only embedded when their layout is
    # enabled (see "Narrator WAV gating" in AGENTS.md). The base
    # (language-neutral) clips are always kept. Ukrainian clips are
    # named wav/ua_*.wav.
    if(NOT CONFIG_SK_LANG_ENABLE_UA)
        list(FILTER _wavs EXCLUDE REGEX "wav/ua_")
    endif()
    if(_wavs)
        # narrator.c does `#include "audio.h"` and `#include
        # "kb_layout.h"`. Declare both as REQUIRES so their public
        # INCLUDE_DIRS are exposed to our compile line. We also
        # add the include paths explicitly via
        # target_include_directories below as a belt-and-suspenders
        # failsafe -- with EMBED_FILES in the mix the IDF
        # dependency-graph expansion has been observed to fail to
        # propagate them on some setups (ESP-IDF v5.5.x), producing
        # `fatal error: audio.h: No such file or directory`.
        idf_component_register(
            SRCS "src/narrator.c"
            INCLUDE_DIRS "include"
            REQUIRES audio kb_layout
            EMBED_FILES ${_wavs}
        )
        target_include_directories(${COMPONENT_LIB} PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../audio/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../kb_layout/include"
        )
    else()
        # No assets dropped in yet -- fall back to the no-op stub
        # so the firmware still builds. Without at least one file,
        # idf_component_register(EMBED_FILES) leaves the component
        # half-registered and downstream sources lose visibility
        # of audio.h (see PRIV_REQUIRES propagation).
        message(WARNING
            "narrator: CONFIG_NARRATOR_ENABLE is set but "
            "components/narrator/wav/ has no .wav files. "
            "Building the no-op stub. Drop the upstream "
            "smart-keyboard audio/*.wav pack into "
            "components/narrator/wav/ to enable spoken letters.")
        idf_component_register(
            SRCS "src/narrator_stub.c"
            INCLUDE_DIRS "include"
            REQUIRES kb_layout
        )
    endif()
else()
    # Stub mode: every public function compiles to a no-op so
    # callers don't need to #ifdef.
    idf_component_register(
        SRCS "src/narrator_stub.c"
        INCLUDE_DIRS "include"
        REQUIRES kb_layout
    )
endif()
