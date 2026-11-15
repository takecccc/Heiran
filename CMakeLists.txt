cmake_minimum_required(VERSION 3.24)

project(Heiran VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_SUPPRESS_REGENERATION OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(HEIRAN_ENABLE_ASAN "Build with AddressSanitizer for runtime memory checks." OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Concurrent Core Gui Svg Widgets OpenGL OpenGLWidgets PrintSupport Test LinguistTools)

if(HEIRAN_ENABLE_ASAN)
  if(MSVC)
    # AddressSanitizer は /RTC と併用できず、増分リンクとも競合する。
    string(REGEX REPLACE "/RTC[1csu]*" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    add_compile_options(/fsanitize=address /Zi)
    add_link_options(/INCREMENTAL:NO /DEBUG)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
      execute_process(COMMAND "${CMAKE_CXX_COMPILER}" -print-resource-dir
        OUTPUT_VARIABLE heiran_clang_resource_dir OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
      set(heiran_clang_runtime_paths "${heiran_clang_resource_dir}/lib/windows"
        "${heiran_clang_resource_dir}/lib/x86_64-pc-windows-msvc")
      find_library(HEIRAN_CLANG_ASAN_RUNTIME clang_rt.asan_dynamic-x86_64
        PATHS ${heiran_clang_runtime_paths} NO_DEFAULT_PATH REQUIRED)
      find_library(HEIRAN_CLANG_ASAN_THUNK clang_rt.asan_dynamic_runtime_thunk-x86_64
        PATHS ${heiran_clang_runtime_paths} NO_DEFAULT_PATH REQUIRED)
      link_libraries("${HEIRAN_CLANG_ASAN_RUNTIME}")
      add_link_options("/WHOLEARCHIVE:${HEIRAN_CLANG_ASAN_THUNK}")
    endif()
  else()
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address)
  endif()
  message(STATUS "AddressSanitizer is enabled.")
endif()


# 比較計測用の PNG デコーダ。既定の経路には使わない。
option(HEIRAN_USE_LIBSPNG "Enable the libspng decoder for benchmarking." ON)

set(HEIRAN_HAVE_LIBSPNG OFF)
if(HEIRAN_USE_LIBSPNG)
  find_package(SPNG CONFIG QUIET)
  if(SPNG_FOUND)
    set(HEIRAN_HAVE_LIBSPNG ON)
    message(STATUS "Using libspng for benchmarking.")
  else()
    message(STATUS "libspng was not found. The spng backend is unavailable.")
  endif()
endif()

# WebP の復号 (静止画とアニメーション)。Qt の WebP プラグインは標準の配布に含まれず、
# 無いと WebP は開けない (標準の Qt に WebP プラグインは含まれない)。
option(HEIRAN_USE_LIBWEBP "Enable libwebp for decoding WebP (still and animated)." ON)

set(HEIRAN_HAVE_LIBWEBP OFF)
if(HEIRAN_USE_LIBWEBP)
  find_package(WebP CONFIG QUIET)
  if(WebP_FOUND)
    set(HEIRAN_HAVE_LIBWEBP ON)
    message(STATUS "Using libwebp for WebP decoding.")
  else()
    message(STATUS "libwebp was not found. WebP files cannot be opened.")
  endif()
endif()

# アニメーション GIF を、再生しながら圧縮して貯めるために使う。無ければ GIF は全部を展開する。
find_package(zstd CONFIG QUIET)
set(HEIRAN_HAVE_ZSTD OFF)
if(zstd_FOUND)
  set(HEIRAN_HAVE_ZSTD ON)
  message(STATUS "Using zstd for the animated GIF frame store.")
else()
  message(STATUS "zstd was not found. Animated GIFs are always fully expanded.")
endif()

# AVIF の復号。AV1 の復号だけを持つ libavif (dav1d) を使う。HEVC を伴う libheif は使わない。
# 無ければ AVIF は開けない。
find_package(libavif CONFIG QUIET)
set(HEIRAN_HAVE_LIBAVIF OFF)
if(libavif_FOUND)
  set(HEIRAN_HAVE_LIBAVIF ON)
  message(STATUS "Using libavif for AVIF decoding.")
else()
  message(STATUS "libavif was not found. AVIF files cannot be opened.")
endif()

# JPEG XL の復号。libjxl は CMake の設定ファイルを持たないため、ヘッダとライブラリを直に探す。
find_path(HEIRAN_JXL_INCLUDE_DIR jxl/decode.h)
find_library(HEIRAN_JXL_LIBRARY NAMES jxl)
find_library(HEIRAN_JXL_THREADS_LIBRARY NAMES jxl_threads)
set(HEIRAN_HAVE_LIBJXL OFF)
if(HEIRAN_JXL_INCLUDE_DIR AND HEIRAN_JXL_LIBRARY AND HEIRAN_JXL_THREADS_LIBRARY)
  set(HEIRAN_HAVE_LIBJXL ON)
  message(STATUS "Using libjxl for JPEG XL decoding.")
else()
  message(STATUS "libjxl was not found. JPEG XL files cannot be opened.")
endif()

# EXR の復号。OpenEXR を直接使う。無ければ、EXR は開けない (Qt は EXR を読めない)。
find_package(OpenEXR CONFIG QUIET)
set(HEIRAN_HAVE_OPENEXR OFF)
if(OpenEXR_FOUND)
  set(HEIRAN_HAVE_OPENEXR ON)
  message(STATUS "Using OpenEXR for EXR decoding.")
else()
  message(STATUS "OpenEXR was not found. EXR files cannot be opened.")
endif()

# TIFF の復号。libtiff を直接使う。無ければ、Qt で読む。
find_package(TIFF QUIET)
set(HEIRAN_HAVE_LIBTIFF OFF)
if(TIFF_FOUND)
  set(HEIRAN_HAVE_LIBTIFF ON)
  message(STATUS "Using libtiff for TIFF decoding.")
else()
  message(STATUS "libtiff was not found. TIFF files are read through Qt.")
endif()

# レンダリングインテントと黒点補正のために使う。Qt には指定する手段が無い。
# 使うのは 3 次元の対応表を組むところだけで、無い場合は Qt の変換へ退避する。
set(HEIRAN_HAVE_LCMS2 OFF)
find_package(lcms2 CONFIG QUIET)
if(lcms2_FOUND)
  set(HEIRAN_HAVE_LCMS2 ON)
  message(STATUS "Using Little-CMS for rendering intents.")
else()
  message(STATUS "Little-CMS was not found. Rendering intents fall back to the Qt transform.")
endif()


add_library(heiran_core
  src/app/ApplicationController.cpp
  src/app/Localization.cpp
  src/app/OpenGlCheck.cpp
  src/app/CrashReporter.cpp
  src/app/SessionController.cpp
  src/app/SessionPathResolver.cpp
  src/domain/CellConfigMapping.cpp
  src/domain/ColorSpace.cpp
  src/domain/ImageTransform.cpp
  src/domain/SessionPages.cpp
  src/domain/ColorDifference.cpp
  src/domain/DiffMetrics.cpp
  src/domain/Scopes.cpp
  src/domain/Ssim.cpp
  src/domain/DiffSettings.cpp
  src/domain/ViewState.cpp
  src/infra/EmbeddedColorProfile.cpp
  src/infra/AnimationComposer.cpp
  src/infra/DpxDecoder.cpp
  src/infra/HdrDecoder.cpp
  src/infra/CmykConversion.cpp
  src/infra/PsdDecoder.cpp
  src/infra/TgaDecoder.cpp
  src/infra/ExifReader.cpp
  src/infra/ImageMetadata.cpp
  src/ui/ActionIcons.cpp
  src/ui/ImageInfoText.cpp
  src/ui/AppStyle.cpp
  src/infra/ApngDecoder.cpp
  src/infra/ExifOrientation.cpp
  src/infra/HrnConfigLoader.cpp
  src/infra/HrnConfigWriter.cpp
  src/infra/ImageDecoder.cpp
  src/ui/AnalysisSampler.cpp
  src/ui/AppSettings.cpp
  src/ui/TransparencyBackground.cpp
  src/ui/CellContextMenu.cpp
  src/ui/AboutDialog.cpp
  src/ui/ManualLocator.cpp
  src/ui/CellOverlayPainter.cpp
  src/ui/CursorStyle.cpp
  src/ui/CellStatusPainter.cpp
  src/ui/CellStatus.cpp
  src/ui/DecodeCache.cpp
  src/ui/DecodeScheduler.cpp
  src/ui/DisplayLink.cpp
  src/ui/FileOperations.cpp
  src/ui/ImagePrinter.cpp
  src/ui/PageListing.cpp
  src/ui/TextureBudgetLedger.cpp
  src/ui/PrefetchPlanner.cpp
  src/ui/RecentImages.cpp
  src/ui/DisplayTransform.cpp
  src/ui/GpuImageRenderer.cpp
  src/ui/DirtyStateController.cpp
  src/ui/DiffBarWidget.cpp
  src/ui/DiffRenderer.cpp
  src/ui/DiffWindow.cpp
  src/ui/AnalysisResultCache.cpp
  src/ui/AnalysisWindowBase.cpp
  src/ui/ChangeGraphSeriesBuilder.cpp
  src/ui/ColorManagement.cpp
  src/ui/ColorTheme.cpp
  src/ui/DisplayProfile.cpp
  src/ui/ChangeGraphWindow.cpp
  src/ui/Histogram3D.cpp
  src/ui/ImageInfo.cpp
  src/ui/ImageInfoWindow.cpp
  src/ui/Histogram3DWindow.cpp
  src/ui/HistogramAxis.cpp
  src/ui/HistogramSeriesBuilder.cpp
  src/ui/HistogramSeriesCache.cpp
  src/ui/HistogramWindow.cpp
  src/ui/ImageSetManagerDialog.cpp
  src/ui/ImageViewWidget.cpp
  src/ui/PageWheelController.cpp
  src/ui/FileDragController.cpp
  src/ui/LoupeWidget.cpp
  src/ui/MainWindow.cpp
  src/ui/PixelInfoWidget.cpp
  src/ui/PageThumbnailDialog.cpp
  src/ui/PixelInfoText.cpp
  src/ui/PixelValueFormat.cpp
  src/ui/ProfileAccumulator.cpp
  src/ui/SessionIssueDialog.cpp
  src/ui/SettingsDialog.cpp
  src/ui/SettingsHelpController.cpp
  src/ui/TransientMessageWidget.cpp
  src/ui/WelcomeWidget.cpp
  src/ui/ViewerChromeController.cpp
  src/ui/ScopeBuilder.cpp
  src/ui/ScopeWindows.cpp
  src/ui/SessionLayoutController.cpp
  src/ui/SessionMutationController.cpp
  src/ui/StartupPreparationController.cpp
  src/ui/FavoritesController.cpp
  src/ui/PageNavigationFilter.cpp
  src/infra/FavoriteStore.cpp
  src/infra/FavoriteExporter.cpp
  src/ui/ThumbnailCache.cpp
  src/ui/ThumbnailLoader.cpp
  src/ui/ViewerGridWidget.cpp
)

target_sources(heiran_core
  PRIVATE
    include/heiran/app/CrashReporter.hpp
    include/heiran/infra/UserDataPaths.hpp
    include/heiran/app/ApplicationController.hpp
    include/heiran/app/Localization.hpp
    include/heiran/app/OpenGlCheck.hpp
    include/heiran/app/SessionController.hpp
    include/heiran/app/SessionPathResolver.hpp
    include/heiran/domain/CellConfigMapping.hpp
    include/heiran/domain/ColorSpace.hpp
    include/heiran/domain/ChannelMode.hpp
    include/heiran/domain/ImageTransform.hpp
    include/heiran/domain/SessionModel.hpp
    include/heiran/domain/SessionPages.hpp
    include/heiran/domain/ColorDifference.hpp
    include/heiran/domain/DiffMetrics.hpp
    include/heiran/domain/Scopes.hpp
    include/heiran/domain/Ssim.hpp
    include/heiran/domain/ViewState.hpp
    include/heiran/infra/EmbeddedColorProfile.hpp
    include/heiran/infra/AnimationComposer.hpp
    include/heiran/infra/GifStream.hpp
    include/heiran/infra/ExifReader.hpp
    include/heiran/infra/ImageMetadata.hpp
    include/heiran/infra/AvifDecoder.hpp
    include/heiran/infra/DpxDecoder.hpp
    include/heiran/infra/ExrDecoder.hpp
    include/heiran/infra/HdrDecoder.hpp
    include/heiran/infra/PsdDecoder.hpp
    include/heiran/infra/RawImage.hpp
    include/heiran/infra/TgaDecoder.hpp
    include/heiran/infra/TiffDecoder.hpp
    include/heiran/infra/JxlDecoder.hpp
    include/heiran/ui/ActionIcons.hpp
    include/heiran/ui/Histogram3D.hpp
    include/heiran/ui/ImageInfo.hpp
    include/heiran/ui/ImageInfoWindow.hpp
    include/heiran/ui/Histogram3DWindow.hpp
    include/heiran/ui/HistogramAxis.hpp
    include/heiran/ui/ImageInfoText.hpp
    include/heiran/ui/AppStyle.hpp
    include/heiran/infra/ApngDecoder.hpp
    include/heiran/infra/ExifOrientation.hpp
    include/heiran/infra/WebpDecoder.hpp
    include/heiran/infra/HrnConfigLoader.hpp
    include/heiran/infra/HrnConfigWriter.hpp
    include/heiran/infra/ImageDecoder.hpp
    include/heiran/infra/ParallelRows.hpp
    include/heiran/ui/AnalysisSampler.hpp
    include/heiran/ui/AppSettings.hpp
    include/heiran/ui/TransparencyBackground.hpp
    include/heiran/ui/CellContextMenu.hpp
    include/heiran/ui/CellOverlayPainter.hpp
    include/heiran/ui/CellStatusPainter.hpp
    include/heiran/ui/CellStatus.hpp
    include/heiran/ui/ChannelMode.hpp
    include/heiran/ui/DecodeCache.hpp
    include/heiran/ui/DecodeScheduler.hpp
    include/heiran/ui/DisplayLink.hpp
    include/heiran/ui/FileOperations.hpp
    include/heiran/ui/ImagePrinter.hpp
    include/heiran/ui/PageListing.hpp
    include/heiran/ui/TextureBudgetLedger.hpp
    include/heiran/ui/PrefetchPlanner.hpp
    include/heiran/ui/RecentImages.hpp
    include/heiran/ui/DisplayTransform.hpp
    include/heiran/ui/GpuImageRenderer.hpp
    include/heiran/ui/DirtyStateController.hpp
    include/heiran/ui/AboutDialog.hpp
    include/heiran/ui/DiffBarWidget.hpp
    include/heiran/ui/DiffWindow.hpp
    include/heiran/ui/AnalysisResultCache.hpp
    include/heiran/ui/AnalysisWindowBase.hpp
    include/heiran/ui/ChangeGraphSeriesBuilder.hpp
    include/heiran/ui/ColorManagement.hpp
    include/heiran/ui/ColorTheme.hpp
    include/heiran/ui/DisplayProfile.hpp
    include/heiran/ui/ChangeGraphWindow.hpp
    include/heiran/ui/HistogramSeriesBuilder.hpp
    include/heiran/ui/HistogramSeriesCache.hpp
    include/heiran/ui/HistogramWindow.hpp
    include/heiran/ui/ImageSetManagerDialog.hpp
    include/heiran/ui/ImageViewWidget.hpp
    include/heiran/ui/LoupeWidget.hpp
    include/heiran/ui/MainWindow.hpp
    include/heiran/ui/PixelInfoWidget.hpp
    include/heiran/ui/PageThumbnailDialog.hpp
    include/heiran/ui/PixelInfoText.hpp
    include/heiran/ui/PixelValueFormat.hpp
    include/heiran/ui/ProfileAccumulator.hpp
    include/heiran/ui/SessionIssueDialog.hpp
    include/heiran/ui/SettingsDialog.hpp
    include/heiran/ui/SettingsHelpController.hpp
    include/heiran/ui/TransientMessageWidget.hpp
    include/heiran/ui/WelcomeWidget.hpp
    include/heiran/ui/ViewerChromeController.hpp
    include/heiran/ui/ScopeBuilder.hpp
    include/heiran/ui/ScopeWindows.hpp
    include/heiran/ui/SessionLayoutController.hpp
    include/heiran/ui/PageWheelController.hpp
    include/heiran/ui/FileDragController.hpp
    include/heiran/ui/SessionMutationController.hpp
    include/heiran/ui/StartupPreparationController.hpp
    include/heiran/ui/FavoritesController.hpp
    include/heiran/ui/PageNavigationFilter.hpp
    include/heiran/infra/FavoriteStore.hpp
    include/heiran/infra/FavoriteExporter.hpp
    include/heiran/ui/ThumbnailCache.hpp
    include/heiran/ui/ThumbnailLoader.hpp
    include/heiran/ui/ViewerGridWidget.hpp
)

target_include_directories(heiran_core
  PUBLIC
    include
)

target_link_libraries(heiran_core
  PUBLIC
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
    Qt6::Svg
    Qt6::Widgets
    Qt6::PrintSupport
    Qt6::OpenGL
    Qt6::OpenGLWidgets
)

# ライセンスの文章。About 画面が読む (実行ファイルへ埋め込むため、配布物のファイルが欠けても見える)。
# THIRD_PARTY_NOTICES.txt は tools/generate_third_party_notices.py が作る。
qt_add_resources(heiran_core heiran_licenses
  PREFIX "/licenses"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}"
  FILES
    LICENSE
    NOTICE
    THIRD_PARTY_NOTICES.txt
)

target_compile_definitions(heiran_core PUBLIC HEIRAN_VERSION_STRING="${PROJECT_VERSION}")

# 線画のアイコン (Lucide、ISC)。静的ライブラリの資源は、使う側の実行ファイルへ引き継がれる。
qt_add_resources(heiran_core heiran_icons
  PREFIX "/icons"
  BASE "resources/icons"
  FILES
    resources/icons/arrow-left.svg
    resources/icons/arrow-right.svg
    resources/icons/clipboard-copy.svg
    resources/icons/contrast.svg
    resources/icons/copy.svg
    resources/icons/external-link.svg
    resources/icons/flip-horizontal.svg
    resources/icons/flip-vertical.svg
    resources/icons/folder-open.svg
    resources/icons/image.svg
    resources/icons/info.svg
    resources/icons/images.svg
    resources/icons/layers.svg
    resources/icons/link.svg
    resources/icons/pencil.svg
    resources/icons/printer.svg
    resources/icons/refresh-cw.svg
    resources/icons/rotate-ccw.svg
    resources/icons/rotate-cw.svg
    resources/icons/save.svg
    resources/icons/scan-line.svg
    resources/icons/scan.svg
    resources/icons/settings.svg
    resources/icons/sun-moon.svg
    resources/icons/sun.svg
    resources/icons/tag.svg
    resources/icons/star.svg
    resources/icons/trash-2.svg
    resources/icons/unlink.svg
    resources/icons/x.svg
    resources/icons/zoom-in.svg
)

if(WIN32)
  # 画面へ割り当てられている ICC プロファイルを GetICMProfile で引くために要る。
  # dbghelp は落ちたときのミニダンプ書き出しに使う。
  # dxgi は画面の更新を待つために使う。
  target_link_libraries(heiran_core PRIVATE gdi32 dbghelp dxgi)
endif()

if(HEIRAN_HAVE_LCMS2)
  target_link_libraries(heiran_core PRIVATE lcms2::lcms2)
  target_compile_definitions(heiran_core PRIVATE HEIRAN_HAVE_LCMS2=1)
endif()


if(HEIRAN_HAVE_ZSTD)
  target_sources(heiran_core PRIVATE src/infra/GifStream.cpp)
  target_link_libraries(heiran_core PUBLIC zstd::libzstd)
  target_compile_definitions(heiran_core PUBLIC HEIRAN_HAVE_ZSTD=1)
endif()

if(HEIRAN_HAVE_LIBAVIF)
  target_sources(heiran_core PRIVATE src/infra/AvifDecoder.cpp)
  target_link_libraries(heiran_core PRIVATE avif)
  target_compile_definitions(heiran_core PUBLIC HEIRAN_HAVE_LIBAVIF=1)
endif()

if(HEIRAN_HAVE_LIBTIFF)
  target_sources(heiran_core PRIVATE src/infra/TiffDecoder.cpp)
  target_link_libraries(heiran_core PRIVATE TIFF::TIFF)
  target_compile_definitions(heiran_core PUBLIC HEIRAN_HAVE_LIBTIFF=1)
endif()

if(HEIRAN_HAVE_OPENEXR)
  target_sources(heiran_core PRIVATE src/infra/ExrDecoder.cpp)
  target_link_libraries(heiran_core PRIVATE OpenEXR::OpenEXR)
  target_compile_definitions(heiran_core PUBLIC HEIRAN_HAVE_OPENEXR=1)
endif()

if(HEIRAN_HAVE_LIBJXL)
  target_sources(heiran_core PRIVATE src/infra/JxlDecoder.cpp)
  target_include_directories(heiran_core PRIVATE ${HEIRAN_JXL_INCLUDE_DIR})
  target_link_libraries(heiran_core PRIVATE ${HEIRAN_JXL_LIBRARY} ${HEIRAN_JXL_THREADS_LIBRARY})
  target_compile_definitions(heiran_core PUBLIC HEIRAN_HAVE_LIBJXL=1)
endif()

if(HEIRAN_HAVE_LIBWEBP)
  target_sources(heiran_core PRIVATE src/infra/WebpDecoder.cpp)
  target_link_libraries(heiran_core PUBLIC WebP::webp WebP::webpdemux)
  target_compile_definitions(heiran_core PUBLIC HEIRAN_HAVE_LIBWEBP=1)
endif()

if(HEIRAN_HAVE_LIBSPNG)
  target_link_libraries(heiran_core PUBLIC $<IF:$<TARGET_EXISTS:spng::spng>,spng::spng,spng::spng_static>)
  target_compile_definitions(heiran_core PUBLIC HEIRAN_HAVE_LIBSPNG=1)
endif()

target_compile_definitions(heiran_core
  PUBLIC
    QT_NO_KEYWORDS
)

# 画面の文言の翻訳。コードの文言は英語で書き (tr)、日本語は resources/i18n/heiran_ja.ts に持つ。.qm は、実行ファイルへ埋め込む。
# .ts を更新するには、`cmake --build build --target update_translations` を実行する。
qt_add_translations(heiran_core
  TS_FILES resources/i18n/heiran_ja.ts
  RESOURCE_PREFIX "/i18n"
  LUPDATE_OPTIONS -no-obsolete
)

set(HEIRAN_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/heiran.ico")

add_executable(heiran
  src/main.cpp
  # The .rc gives Explorer and the task bar an icon, the .qrc gives the window
  # one. The .rc is not read on other platforms, so both are needed.
  resources/heiran.qrc
  $<$<BOOL:${WIN32}>:resources/heiran.rc>
)

target_link_libraries(heiran PRIVATE heiran_core)

if(WIN32)
  # .rc が参照する .ico はビルドシステムから見えない。明示しないと、
  # アイコンを作り直しても再コンパイルされず古いアイコンのまま残る。
  set_property(SOURCE resources/heiran.rc APPEND PROPERTY OBJECT_DEPENDS "${HEIRAN_APP_ICON}")
endif()

# エクスプローラやスタートメニューから起動したときにコンソールが開かないようにする。
# コンソールから起動した場合は親のコンソールを引き継ぐため、qDebug の出力は従来どおり見える。
set_target_properties(heiran PROPERTIES WIN32_EXECUTABLE TRUE)

# 計測用。ctest には登録せず、必要なときだけ実行する。
add_executable(heiran_decode_benchmark
  tools/DecodeBenchmark.cpp
)

target_link_libraries(heiran_decode_benchmark PRIVATE heiran_core)

# 差分 (輝度の差と ΔE2000) の計測用。
add_executable(heiran_diff_benchmark
  tools/DiffBenchmark.cpp
)

target_link_libraries(heiran_diff_benchmark PRIVATE heiran_core)

# パン、ズーム、hover の計測用。イベントを合成して画面描画まで含めて測る。
add_executable(heiran_swap_clock_probe
  tools/SwapClockProbe.cpp
)

target_link_libraries(heiran_swap_clock_probe
  PRIVATE
    heiran_core
)

add_executable(heiran_interaction_benchmark
  tools/InteractionBenchmark.cpp
)

target_link_libraries(heiran_interaction_benchmark PRIVATE heiran_core)

add_executable(heiran_startup_benchmark src/main.cpp)
target_compile_definitions(heiran_startup_benchmark PRIVATE HEIRAN_STARTUP_BENCHMARK)
target_link_libraries(heiran_startup_benchmark PRIVATE heiran_core)

# 見た目の確認用。主ウィンドウを開いて PNG へ書き出す。
add_executable(heiran_screenshot
  tools/ScreenshotTool.cpp
)

target_link_libraries(heiran_screenshot PRIVATE heiran_core)

# 別の書き手 (ImageMagick、ffmpeg) が作ったファイルを復号して、期待値と比べる。
# tools/generate_cross_writer_samples.py が、ファイルと manifest を作る。
add_executable(heiran_cross_writer_check
  tools/CrossWriterCheck.cpp
)

target_link_libraries(heiran_cross_writer_check PRIVATE heiran_core)

add_executable(heiran_view_state_tests
  tests/ViewStateTests.cpp
)

target_link_libraries(heiran_view_state_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

enable_testing()
add_test(NAME heiran_view_state_tests COMMAND heiran_view_state_tests)

add_executable(heiran_image_transform_tests
  tests/ImageTransformTests.cpp
)

target_link_libraries(heiran_image_transform_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_image_transform_tests COMMAND heiran_image_transform_tests)

add_executable(heiran_decode_cache_tests
  tests/DecodeCacheTests.cpp
)

target_link_libraries(heiran_decode_cache_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_decode_cache_tests COMMAND heiran_decode_cache_tests)

add_executable(heiran_image_decoder_tests
  tests/ImageDecoderTests.cpp
)

target_link_libraries(heiran_image_decoder_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_image_decoder_tests COMMAND heiran_image_decoder_tests)

add_executable(heiran_pixel_value_format_tests
  tests/PixelValueFormatTests.cpp
)

target_link_libraries(heiran_pixel_value_format_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_pixel_value_format_tests COMMAND heiran_pixel_value_format_tests)

add_executable(heiran_main_window_tests
  tests/MainWindowTests.cpp
)

target_link_libraries(heiran_main_window_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_main_window_tests COMMAND heiran_main_window_tests)

add_executable(heiran_session_issue_dialog_tests
  tests/SessionIssueDialogTests.cpp
)

target_link_libraries(heiran_session_issue_dialog_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_session_issue_dialog_tests COMMAND heiran_session_issue_dialog_tests)

add_executable(heiran_exif_orientation_tests
  tests/ExifOrientationTests.cpp
)

target_link_libraries(heiran_exif_orientation_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_exif_orientation_tests COMMAND heiran_exif_orientation_tests)

add_executable(heiran_diff_window_tests
  tests/DiffWindowTests.cpp
)

target_link_libraries(heiran_diff_window_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_diff_window_tests COMMAND heiran_diff_window_tests)

add_executable(heiran_manual_locator_tests
  tests/ManualLocatorTests.cpp
)

target_link_libraries(heiran_manual_locator_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_manual_locator_tests COMMAND heiran_manual_locator_tests)

add_executable(heiran_animation_samples_tests
  tests/AnimationSamplesTests.cpp
)

target_link_libraries(heiran_animation_samples_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

target_compile_definitions(heiran_animation_samples_tests
  PRIVATE
    HEIRAN_ANIMATION_SAMPLE_DIR="${CMAKE_SOURCE_DIR}/examples/animation"
)

add_test(NAME heiran_animation_samples_tests COMMAND heiran_animation_samples_tests)

add_executable(heiran_manual_content_tests
  tests/ManualContentTests.cpp
)

target_link_libraries(heiran_manual_content_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

target_compile_definitions(heiran_manual_content_tests
  PRIVATE
    HEIRAN_MANUAL_SOURCE_DIR="${CMAKE_SOURCE_DIR}/manual"
)

add_test(NAME heiran_manual_content_tests COMMAND heiran_manual_content_tests)

add_executable(heiran_about_dialog_tests
  tests/AboutDialogTests.cpp
)

target_link_libraries(heiran_about_dialog_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_about_dialog_tests COMMAND heiran_about_dialog_tests)

# THIRD_PARTY_NOTICES.txt が、入れてあるライブラリと合っているか。依存を替えたのに作り直し忘れると失敗する。
# 作り直しは py tools/generate_third_party_notices.py。vcpkg の installed が見える環境でだけ走らせる。
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND AND VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
  add_test(NAME heiran_third_party_notices_up_to_date
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_third_party_notices.py
            --check
            --installed ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}
            --build-dir ${CMAKE_BINARY_DIR}
            --output ${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt)
endif()

add_executable(heiran_cursor_style_tests
  tests/CursorStyleTests.cpp
)

target_link_libraries(heiran_cursor_style_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_cursor_style_tests COMMAND heiran_cursor_style_tests)

add_executable(heiran_diff_renderer_tests
  tests/DiffRendererTests.cpp
)

target_link_libraries(heiran_diff_renderer_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_diff_renderer_tests COMMAND heiran_diff_renderer_tests)

add_executable(heiran_histogram_series_builder_tests
  tests/HistogramSeriesBuilderTests.cpp
)

target_link_libraries(heiran_histogram_series_builder_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_histogram_series_builder_tests COMMAND heiran_histogram_series_builder_tests)

add_executable(heiran_analysis_window_base_tests
  tests/AnalysisWindowBaseTests.cpp
)

target_link_libraries(heiran_analysis_window_base_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_analysis_window_base_tests COMMAND heiran_analysis_window_base_tests)

add_executable(heiran_histogram_series_cache_tests
  tests/HistogramSeriesCacheTests.cpp
)

target_link_libraries(heiran_histogram_series_cache_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_histogram_series_cache_tests COMMAND heiran_histogram_series_cache_tests)

add_executable(heiran_change_graph_tests
  tests/ChangeGraphTests.cpp
)

target_link_libraries(heiran_change_graph_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_change_graph_tests COMMAND heiran_change_graph_tests)

add_executable(heiran_pixel_info_text_tests
  tests/PixelInfoTextTests.cpp
)

target_link_libraries(heiran_pixel_info_text_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_pixel_info_text_tests COMMAND heiran_pixel_info_text_tests)

add_executable(heiran_color_space_tests
  tests/ColorSpaceTests.cpp
)

target_link_libraries(heiran_color_space_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_color_space_tests COMMAND heiran_color_space_tests)

add_executable(heiran_embedded_color_profile_tests
  tests/EmbeddedColorProfileTests.cpp
)

target_link_libraries(heiran_embedded_color_profile_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_embedded_color_profile_tests COMMAND heiran_embedded_color_profile_tests)

add_executable(heiran_color_management_tests
  tests/ColorManagementTests.cpp
)

target_link_libraries(heiran_color_management_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_color_management_tests COMMAND heiran_color_management_tests)

add_executable(heiran_display_profile_tests
  tests/DisplayProfileTests.cpp
)

target_link_libraries(heiran_display_profile_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_display_profile_tests COMMAND heiran_display_profile_tests)

add_executable(heiran_analysis_value_space_tests
  tests/AnalysisValueSpaceTests.cpp
)

target_link_libraries(heiran_analysis_value_space_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_analysis_value_space_tests COMMAND heiran_analysis_value_space_tests)

add_executable(heiran_loupe_widget_tests
  tests/LoupeWidgetTests.cpp
)

target_link_libraries(heiran_loupe_widget_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_loupe_widget_tests COMMAND heiran_loupe_widget_tests)

add_executable(heiran_cell_overlay_painter_tests
  tests/CellOverlayPainterTests.cpp
)

target_link_libraries(heiran_cell_overlay_painter_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_cell_overlay_painter_tests COMMAND heiran_cell_overlay_painter_tests)

add_executable(heiran_cell_status_painter_tests
  tests/CellStatusPainterTests.cpp
)

target_link_libraries(heiran_cell_status_painter_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_cell_status_painter_tests COMMAND heiran_cell_status_painter_tests)

add_executable(heiran_color_theme_tests
  tests/ColorThemeTests.cpp
)

target_link_libraries(heiran_color_theme_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_color_theme_tests COMMAND heiran_color_theme_tests)

add_executable(heiran_thumbnail_loader_tests
  tests/ThumbnailLoaderTests.cpp
)

target_link_libraries(heiran_thumbnail_loader_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_thumbnail_loader_tests COMMAND heiran_thumbnail_loader_tests)

add_executable(heiran_crash_reporter_tests
  tests/CrashReporterTests.cpp
)

target_link_libraries(heiran_crash_reporter_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_crash_reporter_tests COMMAND heiran_crash_reporter_tests)

add_executable(heiran_session_layout_controller_tests
  tests/SessionLayoutControllerTests.cpp
)

target_link_libraries(heiran_session_layout_controller_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_session_layout_controller_tests COMMAND heiran_session_layout_controller_tests)

add_executable(heiran_prefetch_planner_tests
  tests/PrefetchPlannerTests.cpp
)

target_link_libraries(heiran_prefetch_planner_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_prefetch_planner_tests COMMAND heiran_prefetch_planner_tests)

add_executable(heiran_display_link_tests
  tests/DisplayLinkTests.cpp
)

target_link_libraries(heiran_display_link_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_display_link_tests COMMAND heiran_display_link_tests)

add_executable(heiran_profile_accumulator_tests
  tests/ProfileAccumulatorTests.cpp
)

target_link_libraries(heiran_profile_accumulator_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_profile_accumulator_tests COMMAND heiran_profile_accumulator_tests)

add_executable(heiran_cell_status_tests
  tests/CellStatusTests.cpp
)

target_link_libraries(heiran_cell_status_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_cell_status_tests COMMAND heiran_cell_status_tests)

add_executable(heiran_app_settings_tests
  tests/AppSettingsTests.cpp
)

target_link_libraries(heiran_app_settings_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_app_settings_tests COMMAND heiran_app_settings_tests)

add_executable(heiran_user_data_paths_tests tests/UserDataPathsTests.cpp)
target_link_libraries(heiran_user_data_paths_tests PRIVATE heiran_core Qt6::Test)
add_test(NAME heiran_user_data_paths_tests COMMAND heiran_user_data_paths_tests)

add_executable(heiran_file_drag_tests tests/FileDragTests.cpp)
target_link_libraries(heiran_file_drag_tests PRIVATE heiran_core Qt6::Test)
add_test(NAME heiran_file_drag_tests COMMAND heiran_file_drag_tests)

add_executable(heiran_viewer_grid_widget_tests
  tests/ViewerGridWidgetTests.cpp
)

target_link_libraries(heiran_viewer_grid_widget_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_viewer_grid_widget_tests COMMAND heiran_viewer_grid_widget_tests)

add_executable(heiran_gpu_image_renderer_tests
  tests/GpuImageRendererTests.cpp
)

target_link_libraries(heiran_gpu_image_renderer_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_gpu_image_renderer_tests COMMAND heiran_gpu_image_renderer_tests)

add_executable(heiran_hrn_loader_tests
  tests/HrnConfigLoaderTests.cpp
)

target_link_libraries(heiran_hrn_loader_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_hrn_loader_tests COMMAND heiran_hrn_loader_tests)

add_executable(heiran_hrn_writer_tests
  tests/HrnConfigWriterTests.cpp
)

target_link_libraries(heiran_hrn_writer_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_hrn_writer_tests COMMAND heiran_hrn_writer_tests)

# --- Packaging ---------------------------------------------------------------
# Dependency scanning compares normalized paths, so the exclude patterns can use
# forward slashes only.
if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)
endif()

# Only heiran.exe and the DLLs it needs are packaged.
# Test and benchmark executables are left out on purpose.

# heiran.exe goes into bin/ because that is where the Qt deployment script puts the
# Qt libraries and the qt.conf that points at plugins/. Moving the executable out of
# bin/ leaves it without a qt.conf and the platform plugin is not found.
# CPACK_PACKAGE_EXECUTABLES also looks for the executable under bin/.
install(TARGETS heiran
  RUNTIME_DEPENDENCY_SET heiran_runtime_dependencies
  RUNTIME DESTINATION bin COMPONENT application
)

# The Qt bin directory has to be searchable here. Anything the scan cannot resolve
# stops the install with an error, and PRE_EXCLUDE_REGEXES does not keep a name out
# of the unresolved list. The Qt libraries found this way are the same files the
# deployment script below copies.
get_target_property(HEIRAN_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
get_filename_component(HEIRAN_QT_BIN_DIR "${HEIRAN_QMAKE_EXECUTABLE}" DIRECTORY)

# Resolve the whole DLL chain, not just what heiran links directly.
# The decoders (libavif, libjxl, OpenEXR, libtiff, ...) pull in further DLLs that are only
# visible through their own import tables.
install(RUNTIME_DEPENDENCY_SET heiran_runtime_dependencies
  PRE_EXCLUDE_REGEXES "api-ms-win-.*" "ext-ms-.*"
  POST_EXCLUDE_REGEXES ".*/[Ss]ystem32/.*"
  DIRECTORIES "$<TARGET_FILE_DIR:heiran>" "${HEIRAN_QT_BIN_DIR}"
  DESTINATION bin
  COMPONENT application
)

# Qt libraries and plugins. Let Qt decide what a Qt application needs.
# ソフトウェアの OpenGL (opengl32sw.dll、約 21 MB) も入れない。Heiran は、GPU と、OpenGL 3.0 以上のドライバを要件にする (起動時に確かめる。src/app/OpenGlCheck.cpp)。
# Visual C++ ランタイム (vc_redist.x64.exe、約 19 MB。圧縮済みで、LZMA にかけても縮まない) も入れない。代わりに、インストーラが、入っているかを確かめて、案内を出す (resources/nsis/vcruntime_check.nsh.in)。
# Direct3D のシェーダコンパイラ (d3dcompiler_47、dxcompiler、dxil。合わせて約 19 MB) は入れない。Heiran の描画は OpenGL で、Direct3D のシェーダは使わない
# (入れると、配布物が大きくなり、NSIS の圧縮にも約 8 秒余計にかかる)。Direct3D の描画 (QRhi の D3D11 / D3D12) へ移すときは、外す。
qt_generate_deploy_app_script(
  TARGET heiran
  OUTPUT_SCRIPT heiran_deploy_script
  NO_UNSUPPORTED_PLATFORM_ERROR
  DEPLOY_TOOL_OPTIONS --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --no-compiler-runtime --translations ja
)
install(SCRIPT ${heiran_deploy_script} COMPONENT application)

# NSIS shows the license during installation and wants a .txt file.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt" COPYONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt" DESTINATION . COMPONENT application)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/README.md" DESTINATION . COMPONENT application)
# 配布物には、Heiran のライセンスの表記と、依存ライブラリのライセンスの表記を入れる (Qt の LGPL などの条件)。
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE" DESTINATION . COMPONENT application)
# ユーザーマニュアル (scripts/build-manual.ps1 が <build>/manual へ作る)。Help > Manual が開く。
# 作っていなければ、入れずに進む (package.ps1 は、先に作る)。
install(DIRECTORY "${CMAKE_BINARY_DIR}/manual/" DESTINATION doc/manual COMPONENT application OPTIONAL)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.txt" DESTINATION . COMPONENT application)

set(CPACK_PACKAGE_NAME "Heiran")
set(CPACK_PACKAGE_VENDOR "Heiran")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Image viewer that shows several images in sync.")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Heiran")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt")
set(CPACK_PACKAGE_EXECUTABLES "heiran" "Heiran")
set(CPACK_CREATE_DESKTOP_LINKS "heiran")
set(CPACK_VERBATIM_VARIABLES ON)

if(WIN32)
  set(CPACK_GENERATOR "NSIS;ZIP")
  include(cmake/InstallerLocalization.cmake)
  set(heiran_cpack_template_dir "${CMAKE_CURRENT_BINARY_DIR}/cpack-templates")
  heiran_localize_nsis_template("${heiran_cpack_template_dir}")
  set(CPACK_MODULE_PATH "${heiran_cpack_template_dir};${CMAKE_MODULE_PATH}")
  add_test(NAME heiran_installer_localization_tests
    COMMAND ${CMAKE_COMMAND}
      "-DHEIRAN_CPACK_TEMPLATE=${heiran_cpack_template_dir}/NSIS.template.in"
      "-DHEIRAN_CPACK_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/InstallerLocalizationTests.cmake")
  find_program(HEIRAN_MAKENSIS_EXECUTABLE NAMES makensis)
  if(HEIRAN_MAKENSIS_EXECUTABLE)
    add_test(NAME heiran_installer_association_selection_tests
      COMMAND ${CMAKE_COMMAND}
        "-DHEIRAN_MAKENSIS=${HEIRAN_MAKENSIS_EXECUTABLE}"
        "-DHEIRAN_NSIS_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/resources/nsis"
        "-DHEIRAN_ASSOCIATION_TEST_DIR=${CMAKE_CURRENT_BINARY_DIR}/association-selection-test"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/InstallerAssociationSelectionTests.cmake")
  endif()
  set(CPACK_NSIS_PACKAGE_NAME "Heiran")
  set(CPACK_NSIS_DISPLAY_NAME "Heiran")
  # The installer, the uninstaller and the entry in the installed programs list.
  set(CPACK_NSIS_MUI_ICON "${HEIRAN_APP_ICON}")
  set(CPACK_NSIS_MUI_UNIICON "${HEIRAN_APP_ICON}")
  set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\heiran.exe")
  set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
  set(CPACK_NSIS_BRANDING_TEXT "Heiran ${PROJECT_VERSION}")
  file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/nsis" heiran_nsis_asset_dir)
  file(TO_NATIVE_PATH "${HEIRAN_APP_ICON}" heiran_nsis_icon)
  string(CONCAT CPACK_NSIS_INSTALLER_MUI_ICON_CODE
    "!define MUI_ICON \"${heiran_nsis_icon}\"\n"
    "!define MUI_UNICON \"${heiran_nsis_icon}\"\n"
    "!define HEIRAN_NSIS_ASSET_DIR \"${heiran_nsis_asset_dir}\"\n"
    "!include \"${heiran_nsis_asset_dir}\\appearance.nsh\"")
  # Keep the installed application intact until the user starts installation.
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL OFF)
  set(CPACK_NSIS_MODIFY_PATH OFF)

  # Association-only sections and categories are shared with the design preview.
  # The VC++ runtime is not shipped. The installer checks for a runtime at least as new as the toolset
  # used to build Heiran. Read the CRT headers because Clang's version is unrelated to the runtime.
  find_file(HEIRAN_CRT_VERSION_HEADER crtversion.h
    PATHS ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES} ENV INCLUDE NO_DEFAULT_PATH REQUIRED)
  file(STRINGS "${HEIRAN_CRT_VERSION_HEADER}" heiran_crt_minor_definition
    REGEX "^#define[ \t]+_VC_CRT_MINOR_VERSION[ \t]+[0-9]+")
  if(NOT heiran_crt_minor_definition)
    message(FATAL_ERROR "Could not determine the VC++ runtime version from crtversion.h.")
  endif()
  string(REGEX REPLACE ".*_VC_CRT_MINOR_VERSION[ \t]+([0-9]+).*" "\\1"
    HEIRAN_VCRUNTIME_MINOR "${heiran_crt_minor_definition}")
  configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/nsis/vcruntime_check.nsh.in"
    "${CMAKE_CURRENT_BINARY_DIR}/vcruntime_check.nsh" @ONLY)
  file(READ "${CMAKE_CURRENT_BINARY_DIR}/vcruntime_check.nsh" CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS)

  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/nsis/associations_install.nsh"
    CPACK_NSIS_EXTRA_INSTALL_COMMANDS)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/nsis/associations_uninstall.nsh"
    CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS)
else()
  set(CPACK_GENERATOR "TGZ")
endif()

set(CPACK_COMPONENTS_ALL application)
include(CPack)

cpack_add_component(application
  DISPLAY_NAME "Heiran"
  DESCRIPTION "$(HeiranApplicationDescription)"
  REQUIRED
  HIDDEN
)
add_executable(heiran_page_listing_tests
  tests/PageListingTests.cpp
)

target_link_libraries(heiran_page_listing_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_page_listing_tests COMMAND heiran_page_listing_tests)

add_executable(heiran_session_mutation_controller_tests
  tests/SessionMutationControllerTests.cpp
)

target_link_libraries(heiran_session_mutation_controller_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_session_mutation_controller_tests COMMAND heiran_session_mutation_controller_tests)

add_executable(heiran_startup_preparation_tests tests/StartupPreparationTests.cpp)
target_link_libraries(heiran_startup_preparation_tests PRIVATE heiran_core Qt6::Test)
add_test(NAME heiran_startup_preparation_tests COMMAND heiran_startup_preparation_tests)

add_executable(heiran_decode_scheduler_tests tests/DecodeSchedulerTests.cpp)
target_link_libraries(heiran_decode_scheduler_tests PRIVATE heiran_core Qt6::Test)
add_test(NAME heiran_decode_scheduler_tests COMMAND heiran_decode_scheduler_tests)

add_executable(heiran_favorites_tests tests/FavoritesTests.cpp)
target_link_libraries(heiran_favorites_tests PRIVATE heiran_core Qt6::Test)
add_test(NAME heiran_favorites_tests COMMAND heiran_favorites_tests)

add_executable(heiran_session_pages_tests
  tests/SessionPagesTests.cpp
)

target_link_libraries(heiran_session_pages_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_session_pages_tests COMMAND heiran_session_pages_tests)

add_executable(heiran_cell_config_mapping_tests
  tests/CellConfigMappingTests.cpp
)

target_link_libraries(heiran_cell_config_mapping_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_cell_config_mapping_tests COMMAND heiran_cell_config_mapping_tests)

add_executable(heiran_texture_budget_ledger_tests
  tests/TextureBudgetLedgerTests.cpp
)

target_link_libraries(heiran_texture_budget_ledger_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_texture_budget_ledger_tests COMMAND heiran_texture_budget_ledger_tests)

add_executable(heiran_thumbnail_cache_tests
  tests/ThumbnailCacheTests.cpp
)

target_link_libraries(heiran_thumbnail_cache_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_thumbnail_cache_tests COMMAND heiran_thumbnail_cache_tests)

add_executable(heiran_file_operations_tests
  tests/FileOperationsTests.cpp
)

target_link_libraries(heiran_file_operations_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_file_operations_tests COMMAND heiran_file_operations_tests)

add_executable(heiran_image_printer_tests
  tests/ImagePrinterTests.cpp
)

target_link_libraries(heiran_image_printer_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_image_printer_tests COMMAND heiran_image_printer_tests)

add_executable(heiran_recent_images_tests
  tests/RecentImagesTests.cpp
)

target_link_libraries(heiran_recent_images_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_recent_images_tests COMMAND heiran_recent_images_tests)

add_executable(heiran_transient_message_widget_tests
  tests/TransientMessageWidgetTests.cpp
)

target_link_libraries(heiran_transient_message_widget_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_transient_message_widget_tests COMMAND heiran_transient_message_widget_tests)

add_executable(heiran_multi_frame_decode_tests
  tests/MultiFrameDecodeTests.cpp
)

target_link_libraries(heiran_multi_frame_decode_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

if(HEIRAN_HAVE_LIBWEBP)
  # フィクスチャのアニメーション WebP を組み立てるためにエンコーダと mux が要る。
  target_link_libraries(heiran_multi_frame_decode_tests PRIVATE WebP::webp WebP::libwebpmux)
endif()

add_test(NAME heiran_multi_frame_decode_tests COMMAND heiran_multi_frame_decode_tests)

add_executable(heiran_sub_image_viewer_tests
  tests/SubImageViewerTests.cpp
)

target_link_libraries(heiran_sub_image_viewer_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_sub_image_viewer_tests COMMAND heiran_sub_image_viewer_tests)

add_executable(heiran_apng_decode_tests
  tests/ApngDecodeTests.cpp
)

target_link_libraries(heiran_apng_decode_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_apng_decode_tests COMMAND heiran_apng_decode_tests)

add_executable(heiran_apng_stream_tests
  tests/ApngStreamTests.cpp
)

target_link_libraries(heiran_apng_stream_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_apng_stream_tests COMMAND heiran_apng_stream_tests)

add_executable(heiran_app_style_tests
  tests/AppStyleTests.cpp
)

target_link_libraries(heiran_app_style_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_app_style_tests COMMAND heiran_app_style_tests)

add_executable(heiran_action_icons_tests
  tests/ActionIconsTests.cpp
)

target_link_libraries(heiran_action_icons_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_action_icons_tests COMMAND heiran_action_icons_tests)

add_executable(heiran_diff_metrics_tests
  tests/DiffMetricsTests.cpp
)

target_link_libraries(heiran_diff_metrics_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_diff_metrics_tests COMMAND heiran_diff_metrics_tests)

add_executable(heiran_diff_bar_tests
  tests/DiffBarWidgetTests.cpp
)

target_link_libraries(heiran_diff_bar_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_diff_bar_tests COMMAND heiran_diff_bar_tests)

add_executable(heiran_welcome_widget_tests
  tests/WelcomeWidgetTests.cpp
)

target_link_libraries(heiran_welcome_widget_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_welcome_widget_tests COMMAND heiran_welcome_widget_tests)

add_executable(heiran_image_set_manager_tests
  tests/ImageSetManagerDialogTests.cpp
)

target_link_libraries(heiran_image_set_manager_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_image_set_manager_tests COMMAND heiran_image_set_manager_tests)

add_executable(heiran_color_difference_tests
  tests/ColorDifferenceTests.cpp
)

target_link_libraries(heiran_color_difference_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_color_difference_tests COMMAND heiran_color_difference_tests)

add_executable(heiran_ssim_tests
  tests/SsimTests.cpp
)

target_link_libraries(heiran_ssim_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_ssim_tests COMMAND heiran_ssim_tests)

add_executable(heiran_localization_tests
  tests/LocalizationTests.cpp
)

target_link_libraries(heiran_localization_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

target_compile_definitions(heiran_localization_tests PRIVATE
  HEIRAN_TRANSLATION_SOURCE="${CMAKE_SOURCE_DIR}/resources/i18n/heiran_ja.ts"
)

add_test(NAME heiran_localization_tests COMMAND heiran_localization_tests)

add_executable(heiran_opengl_check_tests
  tests/OpenGlCheckTests.cpp
)

target_link_libraries(heiran_opengl_check_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_opengl_check_tests COMMAND heiran_opengl_check_tests)

add_executable(heiran_scopes_tests
  tests/ScopesTests.cpp
)

target_link_libraries(heiran_scopes_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_scopes_tests COMMAND heiran_scopes_tests)

add_executable(heiran_scope_windows_tests
  tests/ScopeWindowsTests.cpp
)

target_link_libraries(heiran_scope_windows_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_scope_windows_tests COMMAND heiran_scope_windows_tests)

add_executable(heiran_histogram_axis_tests
  tests/HistogramAxisTests.cpp
)

target_link_libraries(heiran_histogram_axis_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_histogram_axis_tests COMMAND heiran_histogram_axis_tests)

add_executable(heiran_histogram3d_tests
  tests/Histogram3DTests.cpp
)

target_link_libraries(heiran_histogram3d_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_histogram3d_tests COMMAND heiran_histogram3d_tests)

add_executable(heiran_image_info_tests
  tests/ImageInfoTests.cpp
)

target_link_libraries(heiran_image_info_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_image_info_tests COMMAND heiran_image_info_tests)

# AVIF / JPEG XL / PSD の復号。見本 (examples/formats) は tools/generate_format_samples.py で作る。
add_executable(heiran_format_decode_tests
  tests/FormatDecodeTests.cpp
)

target_link_libraries(heiran_format_decode_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

target_compile_definitions(heiran_format_decode_tests
  PRIVATE
    HEIRAN_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME heiran_format_decode_tests COMMAND heiran_format_decode_tests)

# HDR / TGA / DPX / PSD の自前の復号器。入力は、テストの中でバイト列から組み立てる。
add_executable(heiran_native_format_tests
  tests/NativeFormatTests.cpp
)

target_link_libraries(heiran_native_format_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_native_format_tests COMMAND heiran_native_format_tests)

add_executable(heiran_display_image_conversion_tests
  tests/DisplayImageConversionTests.cpp
)

target_link_libraries(heiran_display_image_conversion_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_display_image_conversion_tests COMMAND heiran_display_image_conversion_tests)

if(HEIRAN_HAVE_LIBTIFF)
  # TIFF の復号 (libtiff を直接使う経路)。入力は libtiff の書き出し API で作る。
  add_executable(heiran_tiff_decode_tests
    tests/TiffDecodeTests.cpp
  )

  target_link_libraries(heiran_tiff_decode_tests
    PRIVATE
      heiran_core
      Qt6::Test
      TIFF::TIFF
  )

  add_test(NAME heiran_tiff_decode_tests COMMAND heiran_tiff_decode_tests)
endif()

if(HEIRAN_HAVE_OPENEXR)
  # EXR の復号 (OpenEXR を直接使う経路)。入力は OpenEXR の書き出し API で作る。
  add_executable(heiran_exr_decode_tests
    tests/ExrDecodeTests.cpp
  )

  target_link_libraries(heiran_exr_decode_tests
    PRIVATE
      heiran_core
      Qt6::Test
      OpenEXR::OpenEXR
  )

  add_test(NAME heiran_exr_decode_tests COMMAND heiran_exr_decode_tests)
endif()

add_executable(heiran_exif_reader_tests
  tests/ExifReaderTests.cpp
)

target_link_libraries(heiran_exif_reader_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_exif_reader_tests COMMAND heiran_exif_reader_tests)

add_executable(heiran_image_info_text_tests
  tests/ImageInfoTextTests.cpp
)

target_link_libraries(heiran_image_info_text_tests
  PRIVATE
    heiran_core
    Qt6::Test
)

add_test(NAME heiran_image_info_text_tests COMMAND heiran_image_info_text_tests)

if(HEIRAN_HAVE_ZSTD)
  add_executable(heiran_gif_stream_tests
    tests/GifStreamTests.cpp
  )

  target_link_libraries(heiran_gif_stream_tests
    PRIVATE
      heiran_core
      Qt6::Test
  )

  add_test(NAME heiran_gif_stream_tests COMMAND heiran_gif_stream_tests)
endif()

if(HEIRAN_HAVE_LIBWEBP)
  # フィクスチャのアニメーション WebP を組み立てるためにエンコーダと mux が要る。
  add_executable(heiran_webp_animation_tests
    tests/WebpAnimationTests.cpp
  )

  target_link_libraries(heiran_webp_animation_tests
    PRIVATE
      heiran_core
      Qt6::Test
      WebP::webp
      WebP::libwebpmux
  )

  add_test(NAME heiran_webp_animation_tests COMMAND heiran_webp_animation_tests)
endif()

# テストは、既定で、画面を持たない (offscreen) プラットフォームで動かす。実際の窓を出すと、テストの間、マウスやキーボードの操作を奪い、
# 前面の窓へフォーカスを移してしまう (全画面や、窓の表示を確かめるテストがあるため)。実際の窓で確かめたいときは、
# `-DHEIRAN_TEST_QPA_PLATFORM=` (空) で、構成し直す。
set(HEIRAN_TEST_QPA_PLATFORM "offscreen" CACHE STRING
  "Qt platform plugin the tests run on (empty: the native platform, which takes over the mouse and keyboard)")
if(HEIRAN_TEST_QPA_PLATFORM)
  get_property(heiran_all_tests DIRECTORY PROPERTY TESTS)
  foreach(heiran_test IN LISTS heiran_all_tests)
    set_property(TEST ${heiran_test} APPEND PROPERTY ENVIRONMENT "QT_QPA_PLATFORM=${HEIRAN_TEST_QPA_PLATFORM}")
    # offscreen は、フォントを持たない。文字を描くテスト (ルーペなど) のため、システムのフォントの場所を教える。
    if(WIN32 AND DEFINED ENV{WINDIR} AND EXISTS "$ENV{WINDIR}/Fonts")
      set_property(TEST ${heiran_test} APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
    endif()
  endforeach()
endif()
