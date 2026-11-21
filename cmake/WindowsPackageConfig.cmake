if(NOT CPACK_GENERATOR STREQUAL "INNOSETUP")
  return()
endif()

if(NOT CPACK_INNOSETUP_EXECUTABLE)
  set(heiran_inno_hints "${CPACK_HEIRAN_INNOSETUP_DIR}")
  foreach(version IN ITEMS 7 6)
    list(APPEND heiran_inno_hints
      "$ENV{ProgramFiles}/Inno Setup ${version}"
      "$ENV{ProgramFiles\(x86\)}/Inno Setup ${version}"
      "$ENV{LocalAppData}/Programs/Inno Setup ${version}")
  endforeach()
  find_program(heiran_iscc_executable NAMES ISCC
    HINTS ${heiran_inno_hints} NO_CACHE)
  set(CPACK_INNOSETUP_EXECUTABLE "${heiran_iscc_executable}")
endif()

if(NOT CPACK_INNOSETUP_EXECUTABLE)
  message(FATAL_ERROR
    "Inno Setup compiler ISCC.exe was not found. Install Inno Setup 6.3 or later, add its compiler directory to PATH, "
    "or place a portable compiler in '${CPACK_HEIRAN_INNOSETUP_DIR}'. "
    "For a ZIP without an installer, use scripts/package.ps1 -Generator ZIP.")
endif()
