# PROTOTYPE runner (ticket #39) - creates a local venv on first run, then opens the notebook.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSCommandPath
$venv = Join-Path $root ".venv-notebook-toys"
$py = Join-Path $venv "Scripts\python.exe"

if (-not (Test-Path $py)) {
    Write-Host "First run: creating venv and installing marimo + numpy + pandas + altair..."
    py -3.12 -m venv $venv
    & $py -m pip install --quiet --upgrade pip
    & $py -m pip install --quiet marimo numpy pandas altair
    & $py -m pip freeze | Select-String "marimo|numpy|pandas|altair" | Write-Host
}

& $py -m marimo run (Join-Path $root "notebook_toys.py")
