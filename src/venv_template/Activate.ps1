# protoST venv activate -- PowerShell (Windows PowerShell and pwsh):
# `. bin/Activate.ps1`
function global:deactivate {
    if (Test-Path Env:_OLD_PATH) {
        $env:PATH = $env:_OLD_PATH
        Remove-Item Env:_OLD_PATH
    }
    Remove-Item Env:STENV -ErrorAction SilentlyContinue
    Remove-Item Function:\deactivate -ErrorAction SilentlyContinue
}

if (Test-Path Env:_OLD_PATH) { deactivate }
$env:_OLD_PATH = $env:PATH
$env:STENV = "@VENV_PATH@"
$env:PATH = (Join-Path $env:STENV "bin") + [IO.Path]::PathSeparator + $env:PATH
