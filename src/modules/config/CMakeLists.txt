add_library(yeet17_config STATIC
    Features.cpp
    Repairs.cpp
    LegacyPanels.cpp
)
target_include_directories(yeet17_config PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(yeet17_config PUBLIC cxx_std_23)
target_link_libraries(yeet17_config PUBLIC yeet17_core)
if(WIN32)
    target_link_libraries(yeet17_config PUBLIC shell32)
endif()
if(WIN32 AND MSVC)
    target_compile_options(yeet17_config PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()
yeet17_add_winrt_package_refs(yeet17_config)
