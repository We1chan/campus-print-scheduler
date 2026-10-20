param(
    [string]$PublicUrl = "",
    [string]$BindAddress = "0.0.0.0",
    [int]$Port = 8787,
    [string]$StateDirectory = "",
    [ValidateRange(10, 1250)][int]$PageMs = 1000,
    [string]$Python = "python",
    [switch]$LiveWorkspace
)
$ErrorActionPreference = "Stop"
$env:PYTHONIOENCODING = 'utf-8'
$taskRoot = $PSScriptRoot
if (-not $StateDirectory) {
    $taskSession = if ($LiveWorkspace) { 'runtime/classroom-live-' + (Get-Date -Format 'yyyyMMdd-HHmmss') } else { 'runtime/classroom-demo' }
    $StateDirectory = Join-Path $taskRoot $taskSession
}
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
$taskCache = Join-Path (Split-Path (Split-Path $taskApp)) 'CMakeCache.txt'
if (Test-Path -LiteralPath $taskCache) {
    foreach ($taskLine in Get-Content -LiteralPath $taskCache) {
        if ($taskLine -match '^CMAKE_PREFIX_PATH:[^=]+=(.+)$') {
            foreach ($taskPrefix in $Matches[1].Split(';')) {
                $taskBin = Join-Path $taskPrefix 'bin'
                if (Test-Path -LiteralPath (Join-Path $taskBin 'Qt6Gui.dll')) { $env:PATH = "$taskBin;$env:PATH" }
            }
        }
        if ($taskLine -match '^CMAKE_CXX_COMPILER:[^=]+=(.+)$') {
            $taskCompilerBin = Split-Path $Matches[1]
            if (Test-Path -LiteralPath $taskCompilerBin) { $env:PATH = "$taskCompilerBin;$env:PATH" }
        }
    }
} elseif (Get-Command qtpaths6 -ErrorAction SilentlyContinue) {
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
    # Prefer a connected physical network, not a VPN/TUN default route.
    $taskAddress = $null
    if (Get-Command Get-NetAdapter -ErrorAction SilentlyContinue) {
        $taskAddress = Get-NetAdapter -Physical | Where-Object Status -eq 'Up' |
            ForEach-Object { Get-NetIPConfiguration -InterfaceIndex $_.InterfaceIndex } |
            Where-Object { $_.IPv4DefaultGateway -and $_.IPv4Address } |
            ForEach-Object { $_.IPv4Address.IPAddress } |
            Where-Object { $_ -notmatch '^(127\.|169\.254\.|198\.(18|19)\.)' } | Select-Object -First 1
    }
    # A UDP connect selects the local address of the default route without
    # sending traffic, as a fallback. -PublicUrl overrides either choice.
    if (-not $taskAddress) {
        $taskAddress = (& $Python -c 'import socket,ipaddress; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.connect((str(ipaddress.IPv4Address(3221225985)),80)); print(s.getsockname()[0]); s.close()').Trim()
        if ($LASTEXITCODE -ne 0) { throw 'Cannot select a LAN address; pass -PublicUrl http://YOUR-LAN-IP:8787.' }
    }
    if (-not $taskAddress) { throw 'Cannot select a LAN address; pass -PublicUrl http://YOUR-LAN-IP:8787.' }
    $PublicUrl = "http://${taskAddress}:$Port"
}
# ArgumentList requires quoting paths that contain spaces on Windows.
$taskArguments = @('--data-dir', ('"' + $StateDirectory + '"'), '--demo-mode', '--page-ms', "$PageMs")
if ($LiveWorkspace) { $taskArguments += @('--dispatch-scenario', '7') }
# The owner window is intentionally visible for classroom projection.
Start-Process -FilePath $taskApp -ArgumentList $taskArguments -WindowStyle Normal
Write-Host "LAN / public URL: $PublicUrl"
Write-Host 'Keep this terminal open. Press Ctrl+C to stop the web gateway.'
& $Python (Join-Path $taskRoot 'tools/classroom_web.py') --coordinator $taskCoordinator --state-dir $StateDirectory --host $BindAddress --port $Port --public-url $PublicUrl --open-browser
exit $LASTEXITCODE
