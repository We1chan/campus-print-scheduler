param(
    [string]$PublicUrl = "",
    [string]$BindAddress = "0.0.0.0",
    [int]$Port = 8787,
    [string]$StateDirectory = "",
    [ValidateRange(10, 1250)][int]$PageMs = 1000,
    [string]$Python = "python"
)
$ErrorActionPreference = "Stop"
$env:PYTHONIOENCODING = 'utf-8'
$taskRoot = $PSScriptRoot
if (-not $StateDirectory) { $StateDirectory = Join-Path $taskRoot 'runtime/classroom-demo' }
$StateDirectory = [IO.Path]::GetFullPath($StateDirectory)

# Prefer the source build so a historical demo package cannot hide new changes.
$taskApp = @(
    (Join-Path $taskRoot 'build/ci/bin/campus-print.exe'),
    (Join-Path $taskRoot 'build/dev/bin/campus-print.exe'),
    (Join-Path $taskRoot 'campus-print.exe')
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $taskApp) { throw 'Build campus-print first; see docs/classroom-demo.md.' }
$taskCoordinator = Join-Path (Split-Path $taskApp) 'print-coordinator.exe'
if (-not (Test-Path -LiteralPath $taskCoordinator)) { throw 'print-coordinator.exe is missing next to campus-print.exe.' }
if (Get-Command qtpaths6 -ErrorAction SilentlyContinue) {
    $taskQtBin = (& qtpaths6 --query QT_INSTALL_BINS).Trim()
    if ($taskQtBin) { $env:PATH = "$taskQtBin;$env:PATH" }
}
$taskDependencies = Join-Path $taskRoot 'runtime/classroom-python'
if (Test-Path -LiteralPath $taskDependencies) {
    $env:PYTHONPATH = "$taskDependencies;$env:PYTHONPATH"
}
& $Python -c 'import qrcode'
if ($LASTEXITCODE -ne 0) {
    throw 'Install QR dependency first: python -m pip install --target runtime/classroom-python -r tools/classroom/requirements.txt'
}
if (-not $PublicUrl) {
    # A UDP connect selects the local address of the default route without
    # sending traffic. Override -PublicUrl when using a different adapter.
    $taskAddress = (& $Python -c 'import socket,ipaddress; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.connect((str(ipaddress.IPv4Address(3221225985)),80)); print(s.getsockname()[0]); s.close()').Trim()
    if ($LASTEXITCODE -ne 0 -or -not $taskAddress) { throw 'Cannot select a LAN address; pass -PublicUrl http://YOUR-LAN-IP:8787.' }
    $PublicUrl = "http://${taskAddress}:$Port"
}
# ArgumentList requires quoting paths that contain spaces on Windows.
$taskArguments = @('--data-dir', ('"' + $StateDirectory + '"'), '--demo-mode', '--page-ms', "$PageMs")
# The owner window is intentionally visible for classroom projection.
Start-Process -FilePath $taskApp -ArgumentList $taskArguments -WindowStyle Normal
Write-Host "LAN / public URL: $PublicUrl"
Write-Host 'Keep this terminal open. Press Ctrl+C to stop the web gateway.'
& $Python (Join-Path $taskRoot 'tools/classroom_web.py') --coordinator $taskCoordinator --state-dir $StateDirectory --host $BindAddress --port $Port --public-url $PublicUrl --open-browser
exit $LASTEXITCODE
