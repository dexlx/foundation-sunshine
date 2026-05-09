# =============================================================================
# Voice Changer Sidecar - Dependency Installer (PowerShell)
# =============================================================================
# Strategy: prefer existing system Python 3.10/3.11; otherwise download
# Python 3.11 embeddable distribution into <install>/voice_changer/python/
# and install pip + RVC dependencies into a self-contained location so we
# never pollute the user's other Python installations.
#
# Mirrors used (China-friendly):
#   - Python:    https://mirrors.huaweicloud.com/python/
#   - PyPI:      https://pypi.tuna.tsinghua.edu.cn/simple
#   - PyTorch:   https://download.pytorch.org/whl/cu121  (CUDA 12.1)
#                or CPU build from PyPI
#   - HF models: https://hf-mirror.com  (set in download_models.py)
# =============================================================================
[CmdletBinding()]
param(
  [switch]$Embed,                                  # Force embeddable Python
  [switch]$Cpu,                                    # Install CPU torch instead of CUDA
  [switch]$SkipModels,                             # Skip pretrained weights download
  [string]$PythonVersion = "3.11.9",
  [string]$PypiIndex = "https://pypi.tuna.tsinghua.edu.cn/simple"
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

# Locate voice_changer directory: this script lives next to it under <install>/scripts/voice_changer/
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Candidates = @(
  (Join-Path $ScriptDir "..\..\voice_changer"),    # installed: scripts/voice_changer/ -> ../../voice_changer
  (Join-Path $ScriptDir "..\voice_changer"),       # source tree
  (Join-Path $ScriptDir "voice_changer")
)
$VcDir = $null
foreach ($c in $Candidates) {
  $resolved = Resolve-Path -ErrorAction SilentlyContinue $c
  if ($resolved -and (Test-Path (Join-Path $resolved "voice_changer_server.py"))) {
    $VcDir = $resolved.Path
    break
  }
}
if (-not $VcDir) {
  Write-Error "Could not locate voice_changer/ directory next to this script."
  exit 1
}
Write-Host "[*] voice_changer dir: $VcDir"

# ---------------------------------------------------------------------------
# Step 1: pick a Python interpreter
# ---------------------------------------------------------------------------
function Test-PythonOk {
  param([string]$Exe)
  if (-not (Test-Path $Exe)) { return $false }
  try {
    $ver = & $Exe -c "import sys; print('%d.%d' % sys.version_info[:2])" 2>$null
    return ($ver -eq "3.10" -or $ver -eq "3.11")
  } catch { return $false }
}

$PyExe = $null
$EmbedDir = Join-Path $VcDir "python"

if (-not $Embed) {
  # Probe common system Python locations
  $sysPyCandidates = @("py", "python", "python3", "python3.11", "python3.10")
  foreach ($cmd in $sysPyCandidates) {
    $found = Get-Command $cmd -ErrorAction SilentlyContinue
    if ($found) {
      $exe = $found.Source
      if ($cmd -eq "py") {
        # try py -3.11 then py -3.10
        foreach ($v in @("-3.11", "-3.10")) {
          $real = & py $v -c "import sys; print(sys.executable)" 2>$null
          if ($LASTEXITCODE -eq 0 -and (Test-PythonOk $real)) { $exe = $real; break }
        }
      }
      if (Test-PythonOk $exe) { $PyExe = $exe; break }
    }
  }
}

if (-not $PyExe) {
  Write-Host "[*] No system Python 3.10/3.11 found, downloading embeddable Python $PythonVersion..."
  if (-not (Test-Path $EmbedDir)) { New-Item -ItemType Directory -Path $EmbedDir | Out-Null }
  $arch = if ([Environment]::Is64BitOperatingSystem) { "amd64" } else { "win32" }
  $zip = Join-Path $env:TEMP "python-$PythonVersion-embed-$arch.zip"
  $url = "https://mirrors.huaweicloud.com/python/$PythonVersion/python-$PythonVersion-embed-$arch.zip"
  Write-Host "    URL: $url"
  Invoke-WebRequest -Uri $url -OutFile $zip
  Expand-Archive -Force -Path $zip -DestinationPath $EmbedDir
  Remove-Item $zip

  # Embeddable distribution doesn't include pip - install it.
  $PyExe = Join-Path $EmbedDir "python.exe"

  # Enable site-packages: comment out the `import site` exclusion in pythonXY._pth
  $pthFile = Get-ChildItem -Path $EmbedDir -Filter "python*._pth" | Select-Object -First 1
  if ($pthFile) {
    $content = Get-Content $pthFile.FullName -Raw
    $content = $content -replace "(?m)^#\s*import site", "import site"
    if ($content -notmatch "import site") { $content += "`nimport site`n" }
    Set-Content -Path $pthFile.FullName -Value $content -NoNewline
  }

  # Install pip via get-pip.py
  $getPip = Join-Path $env:TEMP "get-pip.py"
  Invoke-WebRequest -Uri "https://bootstrap.pypa.io/get-pip.py" -OutFile $getPip
  & $PyExe $getPip --index-url $PypiIndex
  Remove-Item $getPip
}

Write-Host "[*] Using Python: $PyExe"
& $PyExe --version

# ---------------------------------------------------------------------------
# Step 2: install dependencies
# ---------------------------------------------------------------------------
$reqRvc = Join-Path $VcDir "requirements-rvc.txt"
if (-not (Test-Path $reqRvc)) {
  Write-Error "requirements-rvc.txt not found at $reqRvc"
  exit 1
}

# Install torch first (separate index for CUDA)
if ($Cpu) {
  Write-Host "[*] Installing PyTorch (CPU build from PyPI mirror)..."
  & $PyExe -m pip install --index-url $PypiIndex torch
} else {
  Write-Host "[*] Installing PyTorch with CUDA 12.1 support..."
  & $PyExe -m pip install --index-url https://download.pytorch.org/whl/cu121 torch
}
if ($LASTEXITCODE -ne 0) { Write-Warning "torch install failed; continuing with rest"; }

Write-Host "[*] Installing remaining RVC dependencies (numpy/librosa/soundfile/fairseq/faiss/rvc)..."
& $PyExe -m pip install --index-url $PypiIndex -r $reqRvc

if ($LASTEXITCODE -ne 0) {
  Write-Warning "Some dependencies failed. fairseq is known to be unbuildable on Python 3.12+;"
  Write-Warning "if you saw fairseq errors, re-run this script with -Embed to use a bundled Python 3.11."
  exit 1
}

# ---------------------------------------------------------------------------
# Step 3: download pretrained weights
# ---------------------------------------------------------------------------
if (-not $SkipModels) {
  Write-Host "[*] Downloading RVC pretrained weights (HuBERT + RMVPE, ~354 MB) via hf-mirror..."
  $env:HF_ENDPOINT = "https://hf-mirror.com"
  Push-Location $VcDir
  try {
    & $PyExe download_models.py --pretrained
  } finally {
    Pop-Location
  }
}

# ---------------------------------------------------------------------------
# Step 4: write a launcher next to this script that uses the resolved Python
# ---------------------------------------------------------------------------
$launcher = Join-Path $ScriptDir "start-voice-changer-server.bat"
$launcherContent = @"
@echo off
REM Auto-generated by install-voice-changer-deps.ps1
cd /d "$VcDir"
"$PyExe" voice_changer_server.py %*
pause
"@
Set-Content -Path $launcher -Value $launcherContent -Encoding ASCII

Write-Host ""
Write-Host "[OK] Voice changer dependencies installed."
Write-Host "[OK] Launcher created: $launcher"
Write-Host ""
Write-Host "Next steps:"
Write-Host "  1. Place a .pth (and optional .index) RVC voice model under:"
Write-Host "     $VcDir\models\voices\"
Write-Host "  2. Configure Sunshine: AudioVideo tab -> Voice Changer -> backend=ipc,"
Write-Host "     model_path=<full path>, index_path=<full path>"
Write-Host "  3. Start the sidecar: $launcher --backend rvc --opt model_path=...\<your>.pth"
