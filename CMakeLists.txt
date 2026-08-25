cmake_minimum_required(VERSION 3.25)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# YEET17PCSET — unpackaged WinUI 3 desktop app.
# Built only on Windows (VS 2022 + Windows App SDK). Sources are readable on Linux
# because every Win32 / WinRT call is behind #ifdef _WIN32.
project(YEET17PCSET
    VERSION 1.0.5
    DESCRIPTION "YEET17PCSET — Настройка Windows"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Produce YEET17PCSET.exe (never a generic "app.exe").
set(YEET17_TARGET YEET17PCSET)
set(YEET17_NAMESPACE yeet17)

if(NOT WIN32)
    message(WARNING
        "YEET17PCSET is a Windows-only WinUI 3 application. "
        "CMake will still configure the source tree so headers can be browsed, "
        "but the executable cannot be linked on this host.")
endif()

# ---------------------------------------------------------------------------
# Dependencies (vcpkg manifest mode)
# ---------------------------------------------------------------------------
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Overridable in case only a newer Windows 11 SDK is installed (README: 10.0.22621.0 or newer).
set(YEET17_WINDOWS_SDK_VERSION "10.0.22621.0" CACHE STRING "Windows target platform SDK version")

# ---------------------------------------------------------------------------
# WinRT / WinUI 3 code generation.
#
# CMake's Visual Studio generator can drive MSBuild's native XAML build chain
# (MarkupCompilePass1/Pass2, WinRT reference resolution) via VS_PACKAGE_REFERENCES,
# but that path only reliably works for a project laid out exactly the way the
# official VS "Blank App, Packaged/Unpackaged (WinUI 3 in Desktop)" template
# produces one. For a hand-rolled multi-page CMake project it drops most pages'
# generated headers silently and there is no supported way to fix that from
# outside Microsoft's own template - see microsoft/WindowsAppSDK discussion
# #6446, which documents CMake support here as experimental with "gaps with
# MSBuild [...] expected". ARCHITECTURE.md has the full story.
#
# Instead we generate everything ourselves, the way the actively-maintained
# res2k/winui3_cmake reference project does: run cppwinrt.exe and the standalone
# XamlCompiler.exe directly via CMake custom commands, driven by plain file-level
# dependencies CMake resolves correctly regardless of generator. This needs
# WindowsAppSDK 1.6.240923002 specifically - Microsoft dropped the standalone
# tools/net472/XamlCompiler.exe from later 1.6 servicing releases (including the
# 1.6.250108002 GA build), leaving only the MSBuild-task-hosted compiler.
# ---------------------------------------------------------------------------
if(WIN32)
    set(Microsoft_WindowsAppSDK_PATH "$ENV{USERPROFILE}/.nuget/packages/microsoft.windowsappsdk/1.6.240923002")
    set(Microsoft_Windows_CppWinRT_PATH "$ENV{USERPROFILE}/.nuget/packages/microsoft.windows.cppwinrt/2.0.230706.1")
    set(Microsoft_Windows_SDK_BuildTools_PATH "$ENV{USERPROFILE}/.nuget/packages/microsoft.windows.sdk.buildtools/10.0.22621.756")
    set(Microsoft_Web_WebView2_PATH "$ENV{USERPROFILE}/.nuget/packages/microsoft.web.webview2/1.0.2651.64")

    include(win10sdk_root)
    include(find_platform_winmds)
    include(find_appsdk_winmds)
    include(generate_winappsdk)
    include(generate_XamlMetaDataProvider)
    include(winmd_from_idl)
    include(merge_winmds)
    include(run_cppwinrt)
    include(compile_xaml)

    # WindowsAppSDK ships its own WinRT projections under lib/uapX.Y.ZZZZZ/ folders named
    # after the UAP contract version they target, unrelated to our actual Windows SDK kit -
    # 18362 is the newest one this WindowsAppSDK release (1.6.240923002) ships.
    set(YEET17_APPSDK_WINSDK_VERSION 10.0.18362.0)
    set(YEET17_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")

    # Standard WinRT/WindowsAppSDK projection headers (winrt/Microsoft.UI.Xaml.h etc) that
    # every module's pch.h needs, generated once and shared by every target below.
    generate_winappsdk(
        TARGET yeet17_winappsdk
        OUT "${YEET17_GENERATED_DIR}"
        WINSDK_VERSION ${YEET17_APPSDK_WINSDK_VERSION}
        PLATFORM_WINSDK_VERSION ${YEET17_WINDOWS_SDK_VERSION}
        WINMDS YEET17_WINAPPSDK_WINMD_PATHS
    )

    # Every target that compiles pch.h (the exe and every static lib module) needs the same
    # generated WinRT projection headers, not just the final executable.
    function(yeet17_add_winrt_package_refs target_name)
        if(WIN32)
            target_include_directories(${target_name} PRIVATE
                "${YEET17_GENERATED_DIR}"
                # Interop headers (HWND<->AppWindow etc) ship as static files in the package
                # itself, not generated from a .winmd, so they need the package's own include dir.
                "${Microsoft_WindowsAppSDK_PATH}/include"
            )
            add_dependencies(${target_name} yeet17_winappsdk)
        endif()
    endfunction()
else()
    function(yeet17_add_winrt_package_refs target_name)
    endfunction()
endif()

# ---------------------------------------------------------------------------
# Modules first — each is a static lib with a clean public include root.
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/modules)
add_subdirectory(src/persistence)
add_subdirectory(src/ui)

# ---------------------------------------------------------------------------
# Application executable
# ---------------------------------------------------------------------------
set(YEET17_APP_SOURCES
    src/pch.cpp
    src/app/App.xaml.cpp
    src/app/MainWindow.xaml.cpp
    src/app/Elevation.cpp
    src/ui/pages/InstallPage.xaml.cpp
    src/ui/pages/TweaksPage.xaml.cpp
    src/ui/pages/ConfigPage.xaml.cpp
    src/ui/pages/UpdatesPage.xaml.cpp
)

set(YEET17_APP_HEADERS
    src/pch.h
    src/pch_exe.h
    src/app/App.xaml.h
    src/app/MainWindow.xaml.h
    src/app/Elevation.h
)

set(YEET17_XAML
    src/app/App.xaml
    src/app/MainWindow.xaml
    src/ui/pages/InstallPage.xaml
    src/ui/pages/TweaksPage.xaml
    src/ui/pages/ConfigPage.xaml
    src/ui/pages/UpdatesPage.xaml
)

add_executable(${YEET17_TARGET} WIN32
    ${YEET17_APP_SOURCES}
    ${YEET17_APP_HEADERS}
    ${YEET17_XAML}
    resources/app.rc
    app.manifest
)

set_target_properties(${YEET17_TARGET} PROPERTIES
    OUTPUT_NAME YEET17PCSET
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    VS_WINDOWS_TARGET_PLATFORM_VERSION "${YEET17_WINDOWS_SDK_VERSION}"
    VS_WINDOWS_TARGET_PLATFORM_MIN_VERSION "10.0.19041.0"
)

target_precompile_headers(${YEET17_TARGET} PRIVATE src/pch_exe.h)

# ---------------------------------------------------------------------------
# Our own WinRT types (MainWindow + the four pages) + XAML page compilation.
#
# XamlMetaDataProvider.idl/.cpp are generated from the same template MSBuild's
# own CppWinRTAddXamlMetaDataProviderIdl step would otherwise produce.
# MainWindow.idl / <Page>.idl (src/app, src/ui/pages) are hand-written: each
# declares its runtimeclass so MainWindow can hold winrt::yeet17::InstallPage
# etc. as projected types, and so the pages resolve against the merged
# assembly during XAML compilation.
# ---------------------------------------------------------------------------
if(WIN32)
    generate_XamlMetaDataProvider(
        OUTDIR "${CMAKE_CURRENT_BINARY_DIR}/XamlMetaDataProvider"
        IDL_VAR YEET17_XMDP_IDL
        CPP_VAR YEET17_XMDP_CPP
        NAMESPACE "${YEET17_NAMESPACE}"
        PCH "pch.h"
    )

    set(YEET17_WINMD_UNMERGED_DIR "${CMAKE_CURRENT_BINARY_DIR}/winmd_unmerged")
    set(YEET17_WINMD_MERGED_DIR "${CMAKE_CURRENT_BINARY_DIR}/winmd_merged")

    set(YEET17_OWN_IDL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/app/MainWindow.idl"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages/InstallPage.idl"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages/TweaksPage.idl"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages/ConfigPage.idl"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages/UpdatesPage.idl"
        "${YEET17_XMDP_IDL}"
    )
    set(YEET17_UNMERGED_WINMDS "")
    foreach(_idl IN LISTS YEET17_OWN_IDL_SOURCES)
        get_filename_component(_idl_stem "${_idl}" NAME_WE)
        set(_winmd "${YEET17_WINMD_UNMERGED_DIR}/${_idl_stem}.winmd")
        winmd_from_idl(
            OUT "${_winmd}"
            IN "${_idl}"
            # find_platform_package_dir() looks under the installed Windows kit's own
            # References/ folder, not WindowsAppSDK's package-internal uap lib layout.
            WINSDK_VERSION ${YEET17_WINDOWS_SDK_VERSION}
            REF ${YEET17_WINAPPSDK_WINMD_PATHS}
        )
        list(APPEND YEET17_UNMERGED_WINMDS "${_winmd}")
    endforeach()

    merge_winmds(
        TARGET yeet17_merge_winmds
        OUTDIR "${YEET17_WINMD_MERGED_DIR}"
        NAMESPACE "${YEET17_NAMESPACE}"
        IN ${YEET17_UNMERGED_WINMDS}
        REF ${YEET17_WINAPPSDK_WINMD_PATHS}
    )
    set(YEET17_MERGED_WINMD "${YEET17_WINMD_MERGED_DIR}/${YEET17_NAMESPACE}.winmd")

    run_cppwinrt(
        TARGET yeet17_generate_cppwinrt
        IN "${YEET17_MERGED_WINMD}"
        OUT "${YEET17_GENERATED_DIR}"
        COMPONENT "${YEET17_GENERATED_DIR}/sources"
        COMPONENT_NAME "${YEET17_NAMESPACE}"
        PREFIX OPTIMIZE OVERWRITE
        REF ${YEET17_WINAPPSDK_WINMD_PATHS}
        GENERATED_SOURCES YEET17_CPPWINRT_GENERATED_SOURCES
    )
    add_dependencies(yeet17_generate_cppwinrt yeet17_winappsdk yeet17_merge_winmds)
    add_dependencies(${YEET17_TARGET} yeet17_generate_cppwinrt)

    set(YEET17_XAML_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/xaml_generated")
    compile_xaml(
        TARGET ${YEET17_TARGET}_xaml
        OUTPUT_PATH "${YEET17_XAML_GENERATED_DIR}"
        NAMESPACE "${YEET17_NAMESPACE}"
        PCH "pch.h"
        WINSDK_VERSION 10.0.19041.0
        MERGED_WINMD_DIR "${YEET17_WINMD_MERGED_DIR}"
        REF_WINMD ${YEET17_WINAPPSDK_WINMD_PATHS}
        XAML_APPS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/App.xaml"
        XAML_PAGES
            "${CMAKE_CURRENT_SOURCE_DIR}/src/app/MainWindow.xaml"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages/InstallPage.xaml"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages/TweaksPage.xaml"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages/ConfigPage.xaml"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/pages/UpdatesPage.xaml"
        GENERATED_SOURCES YEET17_XAML_GENERATED_SOURCES
    )

    # cppwinrt -comp also writes generated/sources/<Class>.cpp "starting point" stub files
    # (guarded by static_assert(false, ...) - never to be compiled) alongside the real,
    # complete factory glue at generated/<Class>.g.cpp (winrt_make_yeet17_<Class>() plus the
    # projected type's default constructor). MainWindow constructs the page members through
    # their projected default constructors (winrt::yeet17::ConfigPage{} etc), which - unlike
    # MainWindow/App itself, built with winrt::make<T>() directly - go through this factory
    # machinery, so both the per-class glue and module.g.cpp (the activation dispatcher and
    # WINRT_GetActivationFactory/WINRT_CanUnloadNow exports) are needed after all.
    # XamlMetaDataProvider.g.cpp is deliberately excluded here: the generated
    # XamlMetaDataProvider.cpp (see YEET17_XMDP_CPP below) already #includes it directly, with
    # the proper context (its own XamlMetaDataProvider.h). Compiling it again here as a
    # standalone TU would both fail (no context) and duplicate-define its symbols at link time.
    set(YEET17_CPPWINRT_FACTORY_GLUE "")
    foreach(_cls IN ITEMS MainWindow InstallPage TweaksPage ConfigPage UpdatesPage)
        list(APPEND YEET17_CPPWINRT_FACTORY_GLUE "${YEET17_GENERATED_DIR}/${_cls}.g.cpp")
    endforeach()
    # These per-class glue files aren't in run_cppwinrt()'s own OUTPUT list (only module.g.cpp
    # is), so tell CMake not to expect them to exist yet and to build yeet17_generate_cppwinrt
    # (whose custom command writes them, alongside module.g.cpp) before this target needs them.
    set_source_files_properties(${YEET17_CPPWINRT_FACTORY_GLUE} PROPERTIES GENERATED ON)
    target_sources(${YEET17_TARGET} PRIVATE
        ${YEET17_OWN_IDL_SOURCES}
        ${YEET17_XMDP_CPP}
        ${YEET17_CPPWINRT_GENERATED_SOURCES}
        ${YEET17_CPPWINRT_FACTORY_GLUE}
        ${YEET17_XAML_GENERATED_SOURCES}
    )
    # App.xaml.g.hpp ships a default wWinMain (guarded by this macro) for apps that don't
    # provide their own; App.xaml.cpp provides a real one (elevation + bootstrap first).
    target_compile_definitions(${YEET17_TARGET} PRIVATE DISABLE_XAML_GENERATED_MAIN)
    set_source_files_properties(${YEET17_OWN_IDL_SOURCES} PROPERTIES HEADER_FILE_ONLY ON)
    # XamlCompiler.exe mirrors each .xaml file's source-tree path under OUTPUT_PATH (so
    # src/app/MainWindow.xaml.g.h ends up at .../xaml_generated/src/app/MainWindow.xaml.g.h),
    # and XamlTypeInfo.g.cpp #includes "src\app\App.xaml.h" relative to the project root -
    # cover both the per-directory generated headers and the project root itself.
    target_include_directories(${YEET17_TARGET} PRIVATE
        "${YEET17_XAML_GENERATED_DIR}"
        "${YEET17_XAML_GENERATED_DIR}/src/app"
        "${YEET17_XAML_GENERATED_DIR}/src/ui/pages"
        "${CMAKE_CURRENT_BINARY_DIR}/XamlMetaDataProvider"
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()

target_include_directories(${YEET17_TARGET} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
)

target_compile_definitions(${YEET17_TARGET} PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    YEET17_VERSION="${PROJECT_VERSION}"
)

# Link order: our modules, then json/spdlog, then Windows App SDK.
target_link_libraries(${YEET17_TARGET} PRIVATE
    yeet17_core
    yeet17_install
    yeet17_tweaks
    yeet17_config
    yeet17_updates
    yeet17_persistence
    yeet17_ui
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

if(WIN32)
    target_link_libraries(${YEET17_TARGET} PRIVATE
        WindowsApp
        advapi32
        shell32
        ole32
        oleaut32
        srclient
        "${Microsoft_WindowsAppSDK_PATH}/lib/win10-x64/Microsoft.WindowsAppRuntime.Bootstrap.lib"
    )
    # Embed the UAC + DPI manifest (requireAdministrator).
    target_sources(${YEET17_TARGET} PRIVATE app.manifest)
    if(MSVC)
        target_compile_options(${YEET17_TARGET} PRIVATE /W4 /permissive- /Zc:__cplusplus /await:strict)
        target_link_options(${YEET17_TARGET} PRIVATE
            /MANIFEST:EMBED
            /MANIFESTUAC:level='requireAdministrator'
            /SUBSYSTEM:WINDOWS
        )
    endif()
endif()

# Copy catalogs + presets next to the exe so ConfigStore / PackageCatalog can
# find them without depending on cwd (installers often launch from System32).
add_custom_command(TARGET ${YEET17_TARGET} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/catalog"
        "$<TARGET_FILE_DIR:${YEET17_TARGET}>/catalog"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/presets"
        "$<TARGET_FILE_DIR:${YEET17_TARGET}>/presets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:${YEET17_TARGET}>/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${Microsoft_WindowsAppSDK_PATH}/runtimes/win-x64/native/Microsoft.WindowsAppRuntime.Bootstrap.dll"
        "$<TARGET_FILE_DIR:${YEET17_TARGET}>"
    # Unpackaged apps resolve ms-appx:///src/app/MainWindow.xaml (baked into each page's
    # InitializeComponent() by XamlCompiler) relative to the exe's own directory, so the
    # compiled .xbf tree - which XamlCompiler mirrors under xaml_generated/src/... - has to be
    # staged there too, at the same relative path, or LoadComponent fails to find it at runtime.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${YEET17_XAML_GENERATED_DIR}/src"
        "$<TARGET_FILE_DIR:${YEET17_TARGET}>/src"
    COMMENT "Staging catalog, presets, resources, the WinAppSDK bootstrap DLL and compiled XBF pages next to YEET17PCSET.exe"
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${YEET17_APP_SOURCES} ${YEET17_APP_HEADERS} ${YEET17_XAML}
)

# The .xaml files are compiled by compile_xaml() (standalone XamlCompiler.exe), not by MSBuild's
# native XAML item handling; keep them listed (for IDE browsing / source_group) but not built.
set_source_files_properties(${YEET17_XAML} PROPERTIES HEADER_FILE_ONLY ON)

yeet17_add_winrt_package_refs(${YEET17_TARGET})
