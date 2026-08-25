add_library(yeet17_install STATIC
    PackageCatalog.cpp
    WingetClient.cpp
)
target_include_directories(yeet17_install PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_features(yeet17_install PUBLIC cxx_std_23)
target_link_libraries(yeet17_install PUBLIC yeet17_core)
if(WIN32 AND MSVC)
    target_compile_options(yeet17_install PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()
yeet17_add_winrt_package_refs(yeet17_install)
