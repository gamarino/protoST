function deactivate
    if test -n "$_OLD_STPATH"; set -gx STPATH "$_OLD_STPATH"; set -e _OLD_STPATH
    else; set -e STPATH; end
    if test -n "$_OLD_PATH"; set -gx PATH $_OLD_PATH; set -e _OLD_PATH; end
    set -e STENV
    functions -e deactivate
end
set -gx _OLD_PATH $PATH
set -gx STENV "@VENV_PATH@"
set -gx PATH "@VENV_PATH@/bin" $PATH
