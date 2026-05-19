function global:deactivate {
    if (Test-Path Env:_OLD_STPATH) { $env:STPATH = $env:_OLD_STPATH; Remove-Item Env:_OLD_STPATH }
    else                            { Remove-Item Env:STPATH -ErrorAction SilentlyContinue }
    if (Test-Path Env:_OLD_PATH)    { $env:PATH = $env:_OLD_PATH; Remove-Item Env:_OLD_PATH }
    Remove-Item Env:STENV -ErrorAction SilentlyContinue
}
$env:_OLD_PATH = $env:PATH
$env:STENV = "@VENV_PATH@"
$env:PATH = "@VENV_PATH@/bin;$env:PATH"
