# Writes the engine's built-in meshes to engine/resources/meshes, as OBJ files with normals and UVs.
# Run it again after changing a size or the amount of detail:
#   pwsh tools/generate_primitives.ps1
#
# Y is up, a triangle's front is the side its corners go counterclockwise on, and V goes up the texture,
# as in most exported OBJ files. Sizes follow Unity's built-in meshes.

param([string]$OutDir = (Join-Path $PSScriptRoot '..\engine\resources\meshes'))

$ErrorActionPreference = 'Stop'

$Segments = 32      # around the round shapes
$Rings = 16         # from pole to pole of the sphere
$CapsuleRings = 8   # from the pole to the side of each of the capsule's ends

class MeshBuilder
{
    [System.Collections.Generic.List[string]] $Vertices = [System.Collections.Generic.List[string]]::new()
    [System.Collections.Generic.List[string]] $Faces = [System.Collections.Generic.List[string]]::new()
    [int] $Count = 0

    static [string] Number([double]$value)
    {
        $text = $value.ToString('0.######', [System.Globalization.CultureInfo]::InvariantCulture)
        if ($text -eq '-0') { return '0' }
        return $text
    }

    # Gives the vertex's 1-based index, which the faces use for its position, UV and normal alike
    [int] Add([double[]]$position, [double[]]$normal, [double[]]$uv)
    {
        $n = [MeshBuilder]
        $this.Vertices.Add("v $($n::Number($position[0])) $($n::Number($position[1])) $($n::Number($position[2]))")
        $this.Vertices.Add("vt $($n::Number($uv[0])) $($n::Number($uv[1]))")
        $this.Vertices.Add("vn $($n::Number($normal[0])) $($n::Number($normal[1])) $($n::Number($normal[2]))")
        $this.Count++
        return $this.Count
    }

    [void] Triangle([int]$a, [int]$b, [int]$c)
    {
        $this.Faces.Add("f $a/$a/$a $b/$b/$b $c/$c/$c")
    }

    # A flat rectangle around a center, facing along u x v
    [void] Rectangle([double[]]$center, [double[]]$u, [double[]]$v, [double[]]$normal)
    {
        $corners = @(@(-1, -1), @(1, -1), @(1, 1), @(-1, 1))
        $indices = foreach ($corner in $corners)
        {
            $position = @(0..2 | ForEach-Object { $center[$_] + $u[$_] * $corner[0] + $v[$_] * $corner[1] })
            $this.Add($position, $normal, @((($corner[0] + 1) / 2), (($corner[1] + 1) / 2)))
        }

        $this.Triangle($indices[0], $indices[1], $indices[2])
        $this.Triangle($indices[0], $indices[2], $indices[3])
    }

    # Rows of rings from top to bottom, each with segments + 1 vertices so the UVs can wrap.
    # A row is (y, radius, normal y, v), and the normal leans out from the axis by the rest.
    [void] Lathe([object[]]$rows, [int]$segments)
    {
        $first = $this.Count + 1

        foreach ($row in $rows)
        {
            $y = $row[0]; $radius = $row[1]; $normalY = $row[2]; $texV = $row[3]
            $normalOut = [Math]::Sqrt([Math]::Max(0.0, 1.0 - $normalY * $normalY))

            for ($s = 0; $s -le $segments; $s++)
            {
                # Going around from +Z towards +X, which is to the right seen from outside
                $angle = 2.0 * [Math]::PI * $s / $segments
                $sin = [Math]::Sin($angle); $cos = [Math]::Cos($angle)
                [void]$this.Add(@(($sin * $radius), $y, ($cos * $radius)), @(($sin * $normalOut), $normalY, ($cos * $normalOut)), @(($s / $segments), $texV))
            }
        }

        $stride = $segments + 1
        for ($r = 0; $r -lt $rows.Count - 1; $r++)
        {
            for ($s = 0; $s -lt $segments; $s++)
            {
                $a = $first + $r * $stride + $s   # top left, seen from outside
                $b = $a + $stride                 # bottom left
                $c = $b + 1                       # bottom right
                $d = $a + 1                       # top right

                # A row of radius 0 is a pole, where the triangle with two corners on it has no area
                if ($rows[$r + 1][1] -gt 0) { $this.Triangle($a, $b, $c) }
                if ($rows[$r][1] -gt 0) { $this.Triangle($a, $c, $d) }
            }
        }
    }

    # A flat disc facing straight up or down
    [void] Cap([double]$y, [double]$radius, [int]$segments, [bool]$up)
    {
        $normalY = if ($up) { 1.0 } else { -1.0 }
        $center = $this.Add(@(0.0, $y, 0.0), @(0.0, $normalY, 0.0), @(0.5, 0.5))

        $ring = for ($s = 0; $s -le $segments; $s++)
        {
            $angle = 2.0 * [Math]::PI * $s / $segments
            $sin = [Math]::Sin($angle); $cos = [Math]::Cos($angle)
            $this.Add(@(($sin * $radius), $y, ($cos * $radius)), @(0.0, $normalY, 0.0), @((0.5 + $sin * 0.5), (0.5 + $cos * $normalY * 0.5)))
        }

        for ($s = 0; $s -lt $segments; $s++)
        {
            if ($up) { $this.Triangle($center, $ring[$s], $ring[$s + 1]) }
            else { $this.Triangle($center, $ring[$s + 1], $ring[$s]) }
        }
    }

    [void] Save([string]$path, [string]$description)
    {
        $lines = @("# $description", '# Made by tools/generate_primitives.ps1, change that and run it again instead of editing this') + $this.Vertices + $this.Faces
        [System.IO.File]::WriteAllText($path, ($lines -join "`n") + "`n")
    }
}

function Sphere-Rows([double]$radius, [double]$fromAngle, [double]$toAngle, [int]$steps, [double]$offsetY, [double]$height)
{
    for ($k = 0; $k -le $steps; $k++)
    {
        $angle = $fromAngle + ($toAngle - $fromAngle) * $k / $steps
        $y = [Math]::Cos($angle) * $radius + $offsetY
        $ringRadius = [Math]::Sin($angle) * $radius
        if ([Math]::Abs($ringRadius) -lt 1e-9) { $ringRadius = 0.0 }
        , @($y, $ringRadius, [Math]::Cos($angle), (($y + $height / 2) / $height))
    }
}

New-Item -ItemType Directory -Force $OutDir | Out-Null

# 1 m on each side
$cube = [MeshBuilder]::new()
$cube.Rectangle(@(0.5, 0, 0), @(0, 0, -0.5), @(0, 0.5, 0), @(1, 0, 0))
$cube.Rectangle(@(-0.5, 0, 0), @(0, 0, 0.5), @(0, 0.5, 0), @(-1, 0, 0))
$cube.Rectangle(@(0, 0.5, 0), @(0.5, 0, 0), @(0, 0, -0.5), @(0, 1, 0))
$cube.Rectangle(@(0, -0.5, 0), @(0.5, 0, 0), @(0, 0, 0.5), @(0, -1, 0))
$cube.Rectangle(@(0, 0, 0.5), @(0.5, 0, 0), @(0, 0.5, 0), @(0, 0, 1))
$cube.Rectangle(@(0, 0, -0.5), @(-0.5, 0, 0), @(0, 0.5, 0), @(0, 0, -1))
$cube.Save((Join-Path $OutDir 'SM_Cube.obj'), 'A cube, 1 m on each side')

# 1 m across
$sphere = [MeshBuilder]::new()
$sphere.Lathe(@(Sphere-Rows 0.5 0.0 ([Math]::PI) $Rings 0.0 1.0), $Segments)
$sphere.Save((Join-Path $OutDir 'SM_Sphere.obj'), 'A sphere, 1 m across')

# 2 m tall and 1 m across
$cylinder = [MeshBuilder]::new()
$cylinder.Lathe(@(@(1.0, 0.5, 0.0, 1.0), @(-1.0, 0.5, 0.0, 0.0)), $Segments)
$cylinder.Cap(1.0, 0.5, $Segments, $true)
$cylinder.Cap(-1.0, 0.5, $Segments, $false)
$cylinder.Save((Join-Path $OutDir 'SM_Cylinder.obj'), 'A cylinder, 2 m tall and 1 m across')

# 2 m tall and 1 m across: a 1 m cylinder with a half sphere on each end
$capsule = [MeshBuilder]::new()
$top = @(Sphere-Rows 0.5 0.0 ([Math]::PI / 2) $CapsuleRings 0.5 2.0)
$bottom = @(Sphere-Rows 0.5 ([Math]::PI / 2) ([Math]::PI) $CapsuleRings -0.5 2.0)
$capsule.Lathe($top + $bottom, $Segments)
$capsule.Save((Join-Path $OutDir 'SM_Capsule.obj'), 'A capsule, 2 m tall and 1 m across')

# 10 m on each side, facing up
$plane = [MeshBuilder]::new()
$plane.Rectangle(@(0, 0, 0), @(5, 0, 0), @(0, 0, -5), @(0, 1, 0))
$plane.Save((Join-Path $OutDir 'SM_Plane.obj'), 'A plane, 10 m on each side, facing up')

# 1 m on each side, facing +Z
$quad = [MeshBuilder]::new()
$quad.Rectangle(@(0, 0, 0), @(0.5, 0, 0), @(0, 0.5, 0), @(0, 0, 1))
$quad.Save((Join-Path $OutDir 'SM_Quad.obj'), 'A quad, 1 m on each side, facing +Z')

Get-ChildItem $OutDir -Filter *.obj | ForEach-Object { "$($_.Name): $($_.Length) bytes" }
