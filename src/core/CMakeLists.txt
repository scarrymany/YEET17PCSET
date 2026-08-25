add_library(yeet17_core STATIC
    Logger.cpp
    Localization.cpp
    Settings.cpp
    Paths.cpp
    Utf8.cpp
)
target_include_directories(yeet17_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_features(yeet17_core PUBLIC cxx_std_23)
target_compile_definitions(yeet17_core PUBLIC
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
)
target_link_libraries(yeet17_core PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
if(WIN32 AND MSVC)
    target_compile_options(yeet17_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()
yeet17_add_winrt_package_refs(yeet17_core)
