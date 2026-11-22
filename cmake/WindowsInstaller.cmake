set(CPACK_GENERATOR "INNOSETUP;ZIP")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WindowsPackageConfig.cmake")
set(CPACK_HEIRAN_INNOSETUP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.tools/inno-setup")
set(heiran_installer_dir "${CMAKE_CURRENT_SOURCE_DIR}/resources/installer")
set(CPACK_INNOSETUP_LANGUAGES "english;japanese")
set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
set(CPACK_INNOSETUP_ICON_FILE "${HEIRAN_APP_ICON}")
set(CPACK_INNOSETUP_SETUP_WizardImageFile "${heiran_installer_dir}/sidebar.bmp")
set(CPACK_INNOSETUP_SETUP_WizardSmallImageFile "${heiran_installer_dir}/header.bmp")
set(CPACK_INNOSETUP_SETUP_DisableWelcomePage OFF)
set(CPACK_INNOSETUP_SETUP_PrivilegesRequired lowest)
set(CPACK_INNOSETUP_SETUP_PrivilegesRequiredOverridesAllowed dialog)
set(CPACK_INNOSETUP_SETUP_UsePreviousPrivileges no)
set(CPACK_INNOSETUP_SETUP_DefaultDirName "{code:HeiranDefaultDir}")
set(CPACK_INNOSETUP_SETUP_UninstallDisplayIcon "{app}\\bin\\heiran.exe")
set(CPACK_INNOSETUP_SETUP_ChangesAssociations ON)
set(CPACK_INNOSETUP_SETUP_ArchitecturesAllowed x64compatible)
set(CPACK_INNOSETUP_SETUP_ArchitecturesInstallIn64BitMode x64compatible)
set(CPACK_INNOSETUP_SETUP_Compression lzma2)

set(HEIRAN_ISCC_EXECUTABLE "" CACHE FILEPATH "Path to the Inno Setup compiler (ISCC.exe)")
if(HEIRAN_ISCC_EXECUTABLE)
  set(CPACK_INNOSETUP_EXECUTABLE "${HEIRAN_ISCC_EXECUTABLE}")
endif()

# Read the CRT header because Clang's version is unrelated to the runtime it links.
find_file(HEIRAN_CRT_VERSION_HEADER crtversion.h
  PATHS ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES} ENV INCLUDE NO_DEFAULT_PATH REQUIRED)
file(STRINGS "${HEIRAN_CRT_VERSION_HEADER}" heiran_crt_minor_definition
  REGEX "^#define[ \t]+_VC_CRT_MINOR_VERSION[ \t]+[0-9]+")
if(NOT heiran_crt_minor_definition)
  message(FATAL_ERROR "Could not determine the VC++ runtime version from crtversion.h.")
endif()
string(REGEX REPLACE ".*_VC_CRT_MINOR_VERSION[ \t]+([0-9]+).*" "\\1"
  CPACK_INNOSETUP_DEFINE_HeiranVCRuntimeMinor "${heiran_crt_minor_definition}")

# Keep the options, registry writes and removal of unselected entries in sync.
set(heiran_common_extensions png jpg jpeg bmp gif webp avif jxl psd tif tiff)
set(heiran_precision_extensions exr hdr dpx tga)
set(heiran_extension_tasks "")
set(heiran_extension_registry "")
foreach(heiran_category IN ITEMS common precision)
  string(APPEND heiran_extension_tasks
    "Name: ${heiran_category}; Description: \"{cm:Heiran_${heiran_category}}\"; GroupDescription: \"{cm:Heiran_AssociationTypes}\"\n")
  foreach(heiran_extension IN LISTS heiran_${heiran_category}_extensions)
    set(heiran_task "${heiran_category}\\${heiran_extension}")
    string(APPEND heiran_extension_tasks
      "Name: ${heiran_task}; Description: \".${heiran_extension}\"; GroupDescription: \"{cm:Heiran_AssociationTypes}\"\n")
    foreach(heiran_entry IN ITEMS open_with capabilities)
      if(heiran_entry STREQUAL open_with)
        set(heiran_key "Software\\Classes\\.${heiran_extension}\\OpenWithProgids")
        set(heiran_value "Heiran.Image")
        set(heiran_data "ValueType: string; ValueData: \"\"")
      else()
        set(heiran_key "Software\\Heiran\\Capabilities\\FileAssociations")
        set(heiran_value ".${heiran_extension}")
        set(heiran_data "ValueType: string; ValueData: Heiran.Image")
      endif()
      set(heiran_entry_prefix "Root: HKA; Subkey: \"${heiran_key}\"; ValueName: \"${heiran_value}\"")
      string(APPEND heiran_extension_registry
        "${heiran_entry_prefix}; ${heiran_data}; Flags: uninsdeletevalue; Tasks: ${heiran_task}\n"
        "${heiran_entry_prefix}; Flags: deletevalue dontcreatekey; Tasks: not ${heiran_task}\n")
    endforeach()
  endforeach()
endforeach()

configure_file("${heiran_installer_dir}/associations.iss.in"
  "${CMAKE_CURRENT_BINARY_DIR}/installer/associations.iss" @ONLY)
set(CPACK_INNOSETUP_EXTRA_SCRIPTS
  "${heiran_installer_dir}/messages.iss"
  "${CMAKE_CURRENT_BINARY_DIR}/installer/associations.iss")
set(CPACK_INNOSETUP_CODE_FILES
  "${heiran_installer_dir}/setup.pas"
  "${heiran_installer_dir}/association-page.pas")

add_test(NAME heiran_installer_configuration_tests
  COMMAND "${CMAKE_COMMAND}"
    "-DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
    "-DSCRIPT_DIR=${heiran_installer_dir}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/InstallerConfigurationTests.cmake")
