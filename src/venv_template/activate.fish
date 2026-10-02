# protoST venv activate -- fish: `source bin/activate.fish`
function deactivate
    if set -q _OLD_PATH
        set -gx PATH $_OLD_PATH
        set -e _OLD_PATH
    end
    set -e STENV
    functions -e deactivate
end

if set -q _OLD_PATH
    deactivate
end
set -gx _OLD_PATH $PATH
set -gx STENV "@VENV_PATH@"
set -gx PATH "$STENV/bin" $PATH
