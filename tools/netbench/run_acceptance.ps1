# Dev-only harness: unattended start + full A6 acceptance sweep.
# Delete whenever you like.
#
# Starts the framework with INKING_AUTO_NETWORK=1 (see Application.cpp) so no password
# is needed, keeps stdin open (stdin EOF would end the console loop), and runs every
# measurement in docs/NetworkLayerLadder.md section 6.
$ErrorActionPreference = 'Continue'
$env:PYTHONIOENCODING = 'utf-8'
chcp 65001 | Out-Null

$root = 'D:\InkingBackendFramework\InkingBackendFramework'
$exe = Join-Path $root 'build\bin\InkingBackendFramework.exe'
$client = Join-Path $root 'build\bin\stepA2_echo_client.exe'
$py = 'C:\Users\24807\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'

function Start-Framework {
    Get-Process -Name InkingBackendFramework -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 300
    $script:lines = New-Object System.Collections.Concurrent.ConcurrentQueue[string]
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $exe
    $info.WorkingDirectory = (Split-Path $exe)
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.EnvironmentVariables['INKING_AUTO_NETWORK'] = '1'
    $script:proc = [System.Diagnostics.Process]::Start($info)
    $null = Register-ObjectEvent -InputObject $script:proc -EventName OutputDataReceived -Action {
        if ($EventArgs.Data) { $Event.MessageData.Enqueue($EventArgs.Data) }
    } -MessageData $script:lines
    $null = Register-ObjectEvent -InputObject $script:proc -EventName ErrorDataReceived -Action {
        if ($EventArgs.Data) { $Event.MessageData.Enqueue("ERR: $($EventArgs.Data)") }
    } -MessageData $script:lines
    $script:proc.BeginOutputReadLine()
    $script:proc.BeginErrorReadLine()
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 500
        if (Get-NetTCPConnection -State Listen -LocalPort 9000 -ErrorAction SilentlyContinue) { return $true }
    }
    return $false
}

if (-not (Start-Framework)) {
    "FAIL: port 9000 never came up"
    $lines.ToArray() | Select-Object -Last 20
    exit 1
}
"framework pid=$($proc.Id) listening"

""
"### 1) single connection, 1000 rounds (stepA2 regression)"
& $client 1000 hello
"exit=$LASTEXITCODE"

""
"### 2) 15 conns, one echo each (must be 15/15; old A3 shape got 9/15)"
& $py (Join-Path $root 'tools\netbench\hold_conns.py') 15 0 --echo --timeout 3 | Select-Object -Last 3
"exit=$LASTEXITCODE"

""
"### 3) 1000 conns, one echo each (the whole point of A6)"
& $py (Join-Path $root 'tools\netbench\hold_conns.py') 1000 0 --echo --timeout 5 | Select-Object -Last 3
"exit=$LASTEXITCODE"

""
"### 4) 1000 conns held for 10s, count drops, then handle reclamation"
$p = Get-Process -Id $proc.Id
$baseHandles = $p.HandleCount
"baseline handles = $baseHandles"
& $py (Join-Path $root 'tools\netbench\hold_conns.py') 1000 10 --timeout 5 | Select-Object -Last 3
"exit=$LASTEXITCODE"
$p.Refresh(); $peak = $p.HandleCount
Start-Sleep -Seconds 3
$p.Refresh(); $after = $p.HandleCount
"handles while/after 1000 conns = $peak -> $after (baseline $baseHandles)"
if ($after -le $baseHandles + 20) { "PASS: handles returned to baseline" } else { "FAIL: handles leaked" }

""
"### 5) edge cases: RST / half packet / coalesced packet / big message / reuse"
& $py (Join-Path $root 'tools\netbench\edge_cases.py')
"exit=$LASTEXITCODE"

""
"### 6) idle CPU over 5s (must be near zero)"
$p.Refresh(); $c1 = $p.TotalProcessorTime.TotalMilliseconds
Start-Sleep -Seconds 5
$p.Refresh(); $c2 = $p.TotalProcessorTime.TotalMilliseconds
"idle 5s CPU = $([math]::Round($c2 - $c1, 1)) ms; threads = $($p.Threads.Count); RSS = $([math]::Round($p.WorkingSet64 / 1MB, 1)) MB"

""
"### console tail"
if (-not $proc.HasExited) { $proc.Kill() }
Start-Sleep -Milliseconds 800
$lines.ToArray() | Select-Object -Last 10
