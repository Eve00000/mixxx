file(REMOVE_RECURSE
  ".qsb/pattern.frag.qsb"
  ".qsb/pattern.vert.qsb"
  ".qsb/rgb.frag.qsb"
  ".qsb/rgb.vert.qsb"
  ".qsb/rgba.frag.qsb"
  ".qsb/rgba.vert.qsb"
  ".qsb/texture.frag.qsb"
  ".qsb/texture.vert.qsb"
  ".qsb/unicolor.frag.qsb"
  ".qsb/unicolor.vert.qsb"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/rendergraph_gl_other_files.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
