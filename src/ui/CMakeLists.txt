add_library(yeet17_ui STATIC
    ThemeService.cpp
    ThemeService.h
)
target_include_directories(yeet17_ui PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(yeet17_ui PUBLIC cxx_std_23)
target_link_libraries(yeet17_ui PUBLIC
    yeet17_core
    yeet17_install
    yeet17_tweaks
    yeet17_config
    yeet17_updates
    yeet17_persistence
)
if(WIN32 AND MSVC)
    target_compile_options(yeet17_ui PRIVATE /W4 /permissive- /Zc:__cplusplus /await:strict)
endif()
yeet17_add_winrt_package_refs(yeet17_ui)
