!ifndef HEIRAN_ASSOCIATION_FORMATS
!define HEIRAN_ASSOCIATION_FORMATS

!macro HeiranCommonImageFormats APPLY
  !insertmacro ${APPLY} png
  !insertmacro ${APPLY} jpg
  !insertmacro ${APPLY} jpeg
  !insertmacro ${APPLY} bmp
  !insertmacro ${APPLY} gif
  !insertmacro ${APPLY} webp
  !insertmacro ${APPLY} avif
  !insertmacro ${APPLY} jxl
  !insertmacro ${APPLY} psd
  !insertmacro ${APPLY} tif
  !insertmacro ${APPLY} tiff
!macroend

!macro HeiranHighPrecisionFormats APPLY
  !insertmacro ${APPLY} exr
  !insertmacro ${APPLY} hdr
  !insertmacro ${APPLY} dpx
  !insertmacro ${APPLY} tga
!macroend

!macro HeiranImageFormats APPLY
  !insertmacro HeiranCommonImageFormats ${APPLY}
  !insertmacro HeiranHighPrecisionFormats ${APPLY}
!macroend

!endif
