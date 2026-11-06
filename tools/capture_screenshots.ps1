# Takes the screenshots in docs/images from the running editor, on a copy of samples/Demo.
#   pwsh tools/capture_screenshots.ps1 [-Editor <path to Editor.exe>]
#
# Build the editor in Release first. The editor opens on screen while it runs, and the mouse and keyboard are used to
# click through it, so leave them alone until it closes. Windows only, as it uses the Win32 API to find, size, click and
# capture the window.
#
# The editor's own settings in %APPDATA%\Crimson are put back as they were afterwards, and the project copy is removed.
# The clicks go where the panels are in the default layout, for a 1600 x 900 window, so a change to the layout can mean
# moving them (the Shots table below).

param(
    [string]$Editor = (Join-Path $PSScriptRoot '..\cmake-build-release-msys2-ucrt64\editor\Editor.exe'),
    [string]$OutDir = (Join-Path $PSScriptRoot '..\docs\images'),
    [int]$Width = 1600,
    [int]$Height = 900
)

$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;

public static class Win
{
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr window, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr window, ref POINT point);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint x, uint y, uint data, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
'@

# Pixels as the screen has them, so the window's size and the clicks mean what they say at any Windows scaling
[void][Win]::SetProcessDPIAware()

$editorPath = (Resolve-Path $Editor).Path
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$config = Join-Path $env:APPDATA 'Crimson'
$backup = Join-Path ([IO.Path]::GetTempPath()) ('crimson-config-' + [Guid]::NewGuid())

# A path without the user's name in it, as the project manager shows it
$projectRoot = Join-Path $env:PUBLIC 'Documents\Crimson'
$project = Join-Path $projectRoot 'Demo'

New-Item -ItemType Directory -Force $OutDir | Out-Null

function Wait-Window([System.Diagnostics.Process]$process, [int]$seconds)
{
    $deadline = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $deadline)
    {
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) { return $process.MainWindowHandle }
        Start-Sleep -Milliseconds 200
    }
    throw "The editor's window didn't show up"
}

function Set-ClientSize([IntPtr]$window, [int]$width, [int]$height)
{
    [void][Win]::ShowWindow($window, 9) # restored, not maximized
    Start-Sleep -Milliseconds 300

    # The frame around the client area, measured, so the inside ends up the size asked for
    $rect = New-Object Win+RECT
    [void][Win]::GetClientRect($window, [ref]$rect)
    $origin = New-Object Win+POINT
    [void][Win]::ClientToScreen($window, [ref]$origin)

    $frameWidth = 16
    $frameHeight = 39
    [void][Win]::SetWindowPos($window, [IntPtr]::Zero, 40, 40, $width + $frameWidth, $height + $frameHeight, 0x0040)
    Start-Sleep -Milliseconds 300

    [void][Win]::GetClientRect($window, [ref]$rect)
    $actualWidth = $rect.Right - $rect.Left
    $actualHeight = $rect.Bottom - $rect.Top
    [void][Win]::SetWindowPos($window, [IntPtr]::Zero, 40, 40, $width + $frameWidth + ($width - $actualWidth), $height + $frameHeight + ($height - $actualHeight), 0x0040)
    Start-Sleep -Milliseconds 500
}

function Get-ClientOrigin([IntPtr]$window)
{
    $origin = New-Object Win+POINT
    [void][Win]::ClientToScreen($window, [ref]$origin)
    return $origin
}

# A point in the window, as a fraction of its inside or in pixels from its top left
function Click([IntPtr]$window, [double]$x, [double]$y, [switch]$Double, [switch]$Right)
{
    $origin = Get-ClientOrigin $window
    $px = if ($x -le 1) { [int]($x * $Width) } else { [int]$x }
    $py = if ($y -le 1) { [int]($y * $Height) } else { [int]$y }

    [void][Win]::SetForegroundWindow($window)
    [void][Win]::SetCursorPos($origin.X + $px, $origin.Y + $py)
    Start-Sleep -Milliseconds 150

    $down = if ($Right) { 0x0008 } else { 0x0002 }
    $up = if ($Right) { 0x0010 } else { 0x0004 }
    $times = if ($Double) { 2 } else { 1 }

    for ($i = 0; $i -lt $times; $i++)
    {
        [Win]::mouse_event($down, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 40
        [Win]::mouse_event($up, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 60
    }

    Start-Sleep -Milliseconds 400
}

# The mouse out of the way, so no tooltip or hover is in the picture
function Park-Mouse([IntPtr]$window)
{
    $origin = Get-ClientOrigin $window
    [void][Win]::SetCursorPos($origin.X + $Width - 2, $origin.Y + $Height - 2)
    Start-Sleep -Milliseconds 600
}

# What the window shows, or a part of it given as fractions of its inside
function Capture([IntPtr]$window, [string]$name, [double]$left = 0, [double]$top = 0, [double]$right = 1, [double]$bottom = 1)
{
    [void][Win]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 400

    $origin = Get-ClientOrigin $window
    $x = [int]($left * $Width); $y = [int]($top * $Height)
    $w = [int](($right - $left) * $Width); $h = [int](($bottom - $top) * $Height)

    $bitmap = New-Object System.Drawing.Bitmap($w, $h)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($origin.X + $x, $origin.Y + $y, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $graphics.Dispose()

    $file = Join-Path $OutDir $name
    $bitmap.Save($file, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    "  $name"
}

function Close-Editor([System.Diagnostics.Process]$process)
{
    if ($process.HasExited) { return }

    [void][Win]::PostMessage($process.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) # WM_CLOSE
    if (-not $process.WaitForExit(8000)) { $process.Kill() }
}

# What the editor keeps for the user, put aside to come back afterwards
New-Item -ItemType Directory -Force $backup | Out-Null
foreach ($file in 'imgui.ini', 'recent_projects.json', 'settings.json')
{
    $path = Join-Path $config $file
    if (Test-Path $path) { Copy-Item $path (Join-Path $backup $file) }
}

try
{
    # A fresh copy of the demo, so the project in the repo isn't touched
    if (Test-Path $project) { Remove-Item -Recurse -Force $project }
    New-Item -ItemType Directory -Force $projectRoot | Out-Null
    Copy-Item -Recurse (Join-Path $repo 'samples\Demo') $project

    # The default layout, and only the demo in the recent projects
    New-Item -ItemType Directory -Force $config | Out-Null
    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $config 'imgui.ini')
    $now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    @{ projects = @(@{ name = 'Demo'; path = $project; lastOpened = $now; lastScene = '0' }) } | ConvertTo-Json -Depth 4 |
        Set-Content -Encoding utf8 (Join-Path $config 'recent_projects.json')

    "Project manager"
    $process = Start-Process -FilePath $editorPath -PassThru
    $window = Wait-Window $process 30
    Start-Sleep -Seconds 3
    Park-Mouse $window
    Capture $window 'project-manager.png' 0 0 (1000 / $Width) (620 / $Height)
    Close-Editor $process

    Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $config 'imgui.ini')

    "Editor"
    $process = Start-Process -FilePath $editorPath -ArgumentList '--project', "`"$project`"" -PassThru
    $window = Wait-Window $process 60
    Start-Sleep -Seconds 6
    Set-ClientSize $window $Width $Height
    Start-Sleep -Seconds 2

    # Shots: where to click, as fractions of the window, in the default layout
    # The bunny, on the right of the floor in the Scene view, so it is selected with its outline and gizmo
    Click $window 0.58 0.42
    Park-Mouse $window
    Capture $window 'editor-overview.png'

    # A material in the Assets panel: the materials folder in its tree, then its first tile, the bunny's
    Click $window 0.33 0.76
    Click $window 0.46 0.765
    Park-Mouse $window
    Capture $window 'editor-material.png' 0.753 0.05 0.993 0.658
    Capture $window 'editor-assets.png' 0.249 0.615 0.751 0.985

    Close-Editor $process
}
finally
{
    foreach ($file in 'imgui.ini', 'recent_projects.json', 'settings.json')
    {
        $saved = Join-Path $backup $file
        $path = Join-Path $config $file
        if (Test-Path $saved) { Copy-Item -Force $saved $path } else { Remove-Item -Force -ErrorAction SilentlyContinue $path }
    }

    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $backup
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $project
    if ((Test-Path $projectRoot) -and -not (Get-ChildItem $projectRoot)) { Remove-Item -Force $projectRoot }
}
