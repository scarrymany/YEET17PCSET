add_library(yeet17_tweaks STATIC
    TweakEngine.cpp
    RestorePoint.cpp
    UndoStore.cpp
)
target_include_directories(yeet17_tweaks PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(yeet17_tweaks PUBLIC cxx_std_23)
target_link_libraries(yeet17_tweaks PUBLIC yeet17_core)
if(WIN32)
    target_link_libraries(yeet17_tweaks PUBLIC advapi32)
endif()
if(WIN32 AND MSVC)
    target_compile_options(yeet17_tweaks PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()
yeet17_add_winrt_package_refs(yeet17_tweaks)
