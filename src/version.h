#pragma once
// This file contains the basic Mixxx version and prerelease name. This doesn't
// change very often, so this file can be included in other parts of the code
// where the version number needs to be known at the preprocessor stage and
// using the VersionInfo class is not an option.
#define MIXXX_VERSION_MAJOR 2
#define MIXXX_VERSION_MINOR 7
#define MIXXX_VERSION_PATCH 0
#define MIXXX_VERSION_SUFFIX "alpha"
#define MIXXX_BUILD_FLAGS "-pipe;-ffast-math;-funroll-loops;-O3;-fomit-frame-pointer;-mtune=generic;-Wall;-Wextra;$<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>;-Wfloat-conversion;-Werror=return-type;-Wformat=2;-Wformat-security;-Wvla;-Wundef;-fmacro-prefix-map=/home/evelynne/mixxx=."
