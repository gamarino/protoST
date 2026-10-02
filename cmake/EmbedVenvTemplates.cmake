# Writes a C++ header holding the files of src/venv_template, so that
# `protost venv create` needs no template directory at run time: an installed
# or unzipped protost writes the same files as one run from the build tree.
#
#   cmake -DTEMPLATE_DIR=<dir> -DOUTPUT=<header> -P EmbedVenvTemplates.cmake
#
# Each file becomes {name, R"protost_tpl(<contents>)protost_tpl"}. The
# templates must not contain the raw-string terminator; checked below.
file(GLOB _files RELATIVE "${TEMPLATE_DIR}" "${TEMPLATE_DIR}/*")
list(SORT _files)
set(_out "// Generated from src/venv_template by cmake/EmbedVenvTemplates.cmake. Do not edit.\n")
string(APPEND _out "#pragma once\n\nnamespace protoST::venv_templates {\n\n")
string(APPEND _out "struct File { const char* name; const char* text; };\n\n")
string(APPEND _out "inline constexpr File kFiles[] = {\n")
foreach(_name IN LISTS _files)
    file(READ "${TEMPLATE_DIR}/${_name}" _text)
    string(FIND "${_text}" ")protost_tpl\"" _bad)
    if(NOT _bad EQUAL -1)
        message(FATAL_ERROR "${_name} contains the raw-string terminator )protost_tpl\"")
    endif()
    string(APPEND _out "    {\"${_name}\", R\"protost_tpl(${_text})protost_tpl\"},\n")
endforeach()
string(APPEND _out "};\n\n} // namespace protoST::venv_templates\n")
# Rewritten only when it changes, so an unrelated build does not recompile
# Venv.cpp.
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" _old)
    if(_old STREQUAL _out)
        return()
    endif()
endif()
file(WRITE "${OUTPUT}" "${_out}")
