#pragma once

// Precompiled header for the yeet17 exe target only (see CMakeLists.txt's
// target_precompile_headers call). Adds the concrete WinRT authoring headers on top of the
// shared pch.h: the cppwinrt -comp factory glue files (generated/<Class>.g.cpp) have no
// #includes of their own and rely entirely on the force-included precompiled header to see
// factory_implementation::<Class>, which lives in each class's own <Class>.xaml.h - not in the
// cppwinrt-generated <Class>.g.h (that one only has the CRTP base template). This can't live in
// the shared pch.h because <Class>.xaml.h needs the exe-only XAML-generated include dirs, which
// the static lib module targets don't have.
//
// MainWindow.xaml.h already pulls in every page header, so this one include covers all five.
// XamlMetaDataProvider.g.cpp is not compiled standalone (it's #included by the generated
// XamlMetaDataProvider.cpp, which has its own proper include context), so it needs nothing here.
#include "pch.h"

#ifdef _WIN32
#    include "app/MainWindow.xaml.h"
#endif
