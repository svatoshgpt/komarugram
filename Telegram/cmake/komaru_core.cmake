# KomaruGram code written in Rust (Telegram/rust/komaru_core).
#
# A Rust static library carries its own copy of the standard library, and
# two of them in one binary define the same runtime symbols twice. So on
# Windows and macOS prepare.py builds the crate into tdesktop_rust together
# with the other Rust libraries, and only the header is needed here. The
# Linux image builds tdesktop_rust without the repository at hand, so there
# Corrosion builds the crate on its own.

target_include_directories(Telegram
PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/rust/komaru_core/include
)

if (WIN32 OR APPLE)
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.5.1
)
FetchContent_MakeAvailable(Corrosion)

corrosion_import_crate(
    MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/rust/komaru_core/Cargo.toml
    CRATES komaru_core
    CRATE_TYPES staticlib
    LOCKED
)

target_link_libraries(Telegram PRIVATE komaru_core)
