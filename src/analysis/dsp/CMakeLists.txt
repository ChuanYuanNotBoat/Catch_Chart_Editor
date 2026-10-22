# Keep the small numerical backend fast in the interactive Debug editor while
# retaining debug symbols, the Debug CRT and the normal C++ container ABI.
# Runtime checks conflict with /O2, so remove them only in this directory.
if(MSVC)
    string(REGEX REPLACE "/(RTC[1su]+|Od|Ob0)([ \t]|$)" " " CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()
add_library(CatchSpectrumDsp STATIC ../SpectrumAnalysis.cpp)
set_target_properties(CatchSpectrumDsp PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(CatchSpectrumDsp PUBLIC ../..)
target_compile_features(CatchSpectrumDsp PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(CatchSpectrumDsp PRIVATE /utf-8 "$<$<CONFIG:Debug>:/O2>" "$<$<CONFIG:Debug>:/Ob2>")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(CatchSpectrumDsp PRIVATE "$<$<CONFIG:Debug>:-O2>")
endif()
