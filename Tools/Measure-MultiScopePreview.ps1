param([string]$ProjectRoot=(Split-Path $PSScriptRoot -Parent), [string]$PreviewRoot='')
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
public static class GroupPreviewMetrics {
    public static double[] Read(string source,string outlined) {
        using(var a=new Bitmap(source)) using(var b=new Bitmap(outlined)) {
            if(a.Size!=b.Size)throw new Exception("Preview size mismatch");
            int red=0,green=0,darkShape=0; double delta=0;
            var corner=a.GetPixel(0,0); int bg=corner.R+corner.G+corner.B;
            for(int y=0;y<a.Height;y++)for(int x=0;x<a.Width;x++) {
                var p=a.GetPixel(x,y);var q=b.GetPixel(x,y);
                if(q.R>80 && q.R>2*q.G && q.R>2*q.B)red++;
                if(q.G>80 && q.G>2*q.R && q.G>2*q.B)green++;
                // Gray validation surface can be brighter than the blue background.
                // Also count dark textured body pixels; ignore the background gradient.
                bool neutral=Math.Abs(p.R-p.G)<3 && Math.Abs(p.G-p.B)<3 && p.R>30 && p.R<180;
                if(neutral || p.R+p.G+p.B<bg-45)darkShape++;
                delta+=Math.Abs(p.R-q.R)+Math.Abs(p.G-q.G)+Math.Abs(p.B-q.B);
            }
            return new[]{(double)red,(double)green,(double)darkShape,delta/(3*a.Width*a.Height)};
        }
    }
}
'@
$taskPreviewRoot=if($PreviewRoot){$PreviewRoot}else{Join-Path $ProjectRoot 'Documentation\Outline\MultiScope\Validation\Reopen'}
$metrics=@{}
foreach($name in @('GroupPreview','GroupPreviewNone','GroupPreviewGreen','TexturedCharacter')) {
    $folder=Join-Path $taskPreviewRoot $name
    $m=[GroupPreviewMetrics]::Read((Join-Path $folder 'Original.png'),(Join-Path $folder 'Outline.png'))
    $metrics[$name]=@{Red=$m[0];Green=$m[1];SourceShapePixels=$m[2];RGBMeanAbsoluteDifference=$m[3]}
}
$checks=@(
    @{Name='BothViewsContainCharacter';Passed=($metrics.GroupPreview.SourceShapePixels -gt 1000 -and $metrics.GroupPreview.Red -gt 100)},
    @{Name='DisabledOutlineMatchesOriginal';Passed=($metrics.GroupPreviewNone.RGBMeanAbsoluteDifference -lt 1)},
    @{Name='GreenAndWidthUpdateInActorPreview';Passed=($metrics.GroupPreviewGreen.Green -gt $metrics.GroupPreview.Red -and [Math]::Abs($metrics.GroupPreviewGreen.Red-$metrics.GroupPreviewNone.Red) -lt 10)},
    @{Name='TexturedCharacterHasSurfaceAndOutline';Passed=($metrics.TexturedCharacter.SourceShapePixels -gt 1000 -and $metrics.TexturedCharacter.RGBMeanAbsoluteDifference -gt 0.1)}
)
$report=@{Passed=(@($checks|Where-Object {-not $_.Passed}).Count -eq 0);Metrics=$metrics;Checks=$checks}
$report|ConvertTo-Json -Depth 7|Set-Content (Join-Path $taskPreviewRoot 'PreviewPixelMetrics.json') -Encoding UTF8
$report|ConvertTo-Json -Depth 7
if(-not $report.Passed){throw 'Actor preview pixel checks failed.'}
