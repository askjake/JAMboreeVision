param([int]$Port=5004,[string]$BindAddress='0.0.0.0')
$ErrorActionPreference='Stop'
Set-Location $PSScriptRoot
if (-not (Test-Path '.venv\Scripts\python.exe')) {
    py -3 -m venv .venv
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.10+ is required.' }
    & .\.venv\Scripts\python.exe -m pip install -r requirements.txt
    if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
}
& .\.venv\Scripts\python.exe -m vision --host $BindAddress --port $Port
exit $LASTEXITCODE
