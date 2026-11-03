# KomaruGram code written in Rust (Telegram/rust/komaru_core), linked into
# the client as a static library. Corrosion runs cargo as part of the build.

include(FetchContent)

FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.5.1
)

# The 32-bit Windows build runs on a 64-bit host, so say what to build.
if (WIN32 AND NOT build_win64)
    set(Rust_CARGO_TARGET i686-pc-windows-msvc CACHE STRING "" FORCE)
endif()

FetchContent_MakeAvailable(Corrosion)

corrosion_import_crate(
    MANIFEST_PATH ${CMAKE_CURRENT_SOURCE_DIR}/rust/komaru_core/Cargo.toml
    CRATES komaru_core
    CRATE_TYPES staticlib
    LOCKED
)

if (MSVC)
    # The client links the static C runtime.
    corrosion_add_target_rustflags(komaru_core -Ctarget-feature=+crt-static)
endif()

target_include_directories(Telegram
PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/rust/komaru_core/include
)

target_link_libraries(Telegram PRIVATE komaru_core)
