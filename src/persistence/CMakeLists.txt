add_library(yeet17_persistence STATIC
    ConfigStore.cpp
)
target_include_directories(yeet17_persistence PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(yeet17_persistence PUBLIC cxx_std_23)
target_link_libraries(yeet17_persistence PUBLIC yeet17_core yeet17_install yeet17_tweaks yeet17_updates)
if(WIN32 AND MSVC)
    target_compile_options(yeet17_persistence PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()
yeet17_add_winrt_package_refs(yeet17_persistence)
