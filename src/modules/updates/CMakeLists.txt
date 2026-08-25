add_library(yeet17_updates STATIC
    UpdatePolicy.cpp
    SelfUpdate.cpp
)
target_include_directories(yeet17_updates PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(yeet17_updates PUBLIC cxx_std_23)
target_link_libraries(yeet17_updates PUBLIC yeet17_core)
if(WIN32)
    target_link_libraries(yeet17_updates PUBLIC winhttp)
endif()
if(WIN32 AND MSVC)
    target_compile_options(yeet17_updates PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()
yeet17_add_winrt_package_refs(yeet17_updates)
