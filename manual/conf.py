# Heiran のマニュアル (Sphinx)。ビルドは scripts/build-manual.ps1。

project = "Heiran マニュアル"
author = "Atelier Takec"
copyright = "2026 Atelier Takec"
language = "ja"

extensions = ["myst_parser"]
source_suffix = {".md": "markdown"}
root_doc = "index"
exclude_patterns = ["_build"]

myst_enable_extensions = ["colon_fence", "deflist"]
myst_heading_anchors = 3

html_theme = "furo"
html_title = "Heiran マニュアル"
html_static_path = ["_static"]
html_css_files = ["manual.css"]
# 検索は、日本語の分かち書きを使う。ファイルを直に開いても (file://) 動く。
html_search_language = "ja"
html_show_sphinx = False
html_show_copyright = True

# 画面の見出しの語を、メニューの表記のまま書くので、引用符の自動変換は切る。
smartquotes = False
