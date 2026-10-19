param([string]$ProjectRoot = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing.dll -TypeDefinition @'
using System;
using System.Drawing;
public static class NPROutlineImageMetrics {
    public static double[] Compare(string original, string outlined) {
        using (var a = new Bitmap(original)) using (var b = new Bitmap(outlined)) {
            if (a.Width != b.Width || a.Height != b.Height) throw new Exception("Comparison dimensions differ");
            long ar=0, ag=0, br=0, bg=0; double difference=0;
            for(int y=0;y<a.Height;y++) for(int x=0;x<a.Width;x++) {
                var p=a.GetPixel(x,y); var q=b.GetPixel(x,y);
                // Hull color passes through exposure and tonemapping; measure hue,
                // including the darker red response, rather than requiring HDR red.
                if(p.R>40 && p.R>1.6*p.G && p.R>1.6*p.B) ar++;
                if(p.G>40 && p.G>1.6*p.R && p.G>1.6*p.B) ag++;
                if(q.R>40 && q.R>1.6*q.G && q.R>1.6*q.B) br++;
                if(q.G>40 && q.G>1.6*q.R && q.G>1.6*q.B) bg++;
                difference += Math.Abs(p.R-q.R)+Math.Abs(p.G-q.G)+Math.Abs(p.B-q.B);
            }
            return new double[]{a.Width,a.Height,br-ar,bg-ag,difference/(a.Width*a.Height*3.0)};
        }
    }
    private static bool IsDarkOutline(Color source, Color output) {
        return Math.Max(output.R,Math.Max(output.G,output.B)) < 90
            && source.R+source.G+source.B > output.R+output.G+output.B+60;
    }
    public static double[] CompareEffectMasks(string originalA, string outlineA, string originalB, string outlineB) {
        using(var a=new Bitmap(originalA)) using(var b=new Bitmap(outlineA))
        using(var c=new Bitmap(originalB)) using(var d=new Bitmap(outlineB)) {
            if(a.Size!=b.Size || a.Size!=c.Size || a.Size!=d.Size) throw new Exception("Effect mask dimensions differ");
            long first=0,second=0,intersection=0,union=0,nearFirst=0,nearSecond=0;
            var maskA=new bool[a.Width,a.Height]; var maskB=new bool[a.Width,a.Height];
            for(int y=0;y<a.Height;y++) for(int x=0;x<a.Width;x++) {
                bool p=IsDarkOutline(a.GetPixel(x,y),b.GetPixel(x,y));
                bool q=IsDarkOutline(c.GetPixel(x,y),d.GetPixel(x,y));
                maskA[x,y]=p; maskB[x,y]=q;
                if(p) first++; if(q) second++; if(p&&q) intersection++; if(p||q) union++;
            }
            // A one-pixel neighbourhood admits subpixel raster/temporal jitter;
            // also report exact IoU so this tolerance stays explicit.
            for(int y=0;y<a.Height;y++) for(int x=0;x<a.Width;x++) {
                bool foundA=false,foundB=false;
                for(int dy=-1;dy<=1;dy++) for(int dx=-1;dx<=1;dx++) {
                    int xx=x+dx,yy=y+dy;
                    if(xx>=0&&xx<a.Width&&yy>=0&&yy<a.Height) {
                        foundA|=maskA[xx,yy]; foundB|=maskB[xx,yy];
                    }
                }
                if(maskA[x,y]&&foundB) nearFirst++; if(maskB[x,y]&&foundA) nearSecond++;
            }
            return new double[]{first,second,intersection,union,union>0 ? (double)intersection/union : 0,
                first>0 ? (double)nearFirst/first : 0, second>0 ? (double)nearSecond/second : 0};
        }
    }
}
'@
$taskRoot = Join-Path $ProjectRoot 'Saved\NPR\Outline\Validation'
$rows = @()
foreach ($variant in @('None','HullThin','HullWide','ScreenThin','ScreenWide','ScreenGreen','FinalSaved')) {
    $folder = Join-Path $taskRoot ('Create\' + $variant)
    $result = [NPROutlineImageMetrics]::Compare((Join-Path $folder 'Original.png'), (Join-Path $folder 'Outline.png'))
    $rows += [pscustomobject]@{Variant=$variant;Width=$result[0];Height=$result[1];AddedRedPixels=$result[2];AddedGreenPixels=$result[3];MeanAbsoluteRGBDifference=$result[4]}
}
$byName=@{}
foreach($row in $rows){$byName[$row.Variant]=$row}
$checks=@(
    [pscustomobject]@{Name='NoneMatchesOriginal';Passed=($byName.None.MeanAbsoluteRGBDifference -lt 1.0)},
    [pscustomobject]@{Name='HullWidthIncreasesColoredArea';Passed=($byName.HullThin.AddedRedPixels -gt 10 -and $byName.HullWide.AddedRedPixels -gt $byName.HullThin.AddedRedPixels)},
    [pscustomobject]@{Name='ScreenWidthIncreasesColoredArea';Passed=($byName.ScreenThin.AddedRedPixels -gt 100 -and $byName.ScreenWide.AddedRedPixels -gt 1.5*$byName.ScreenThin.AddedRedPixels)},
    [pscustomobject]@{Name='GreenColorChangesPixels';Passed=($byName.ScreenGreen.AddedGreenPixels -gt 100 -and $byName.ScreenGreen.AddedRedPixels -lt $byName.ScreenWide.AddedRedPixels/4)}
)
$reopen = Join-Path $taskRoot 'Reopen\Reopened\Outline.png'
if(Test-Path -LiteralPath $reopen) {
    $comparison=[NPROutlineImageMetrics]::Compare((Join-Path $taskRoot 'Create\FinalSaved\Outline.png'), $reopen)
    $masks=[NPROutlineImageMetrics]::CompareEffectMasks((Join-Path $taskRoot 'Create\FinalSaved\Original.png'), (Join-Path $taskRoot 'Create\FinalSaved\Outline.png'), (Join-Path $taskRoot 'Reopen\Reopened\Original.png'), $reopen)
    # Separate processes can differ in temporal shading. Compare each outline
    # against its own original, then compare the added dark outline masks.
    $checks += [pscustomobject]@{Name='ReopenRetainsOutlineMask';Passed=($masks[0] -gt 100 -and $masks[5] -ge 0.95 -and $masks[6] -ge 0.95 -and [Math]::Abs($masks[1]-$masks[0])/$masks[0] -lt 0.05);SavedMaskPixels=$masks[0];ReopenedMaskPixels=$masks[1];ExactMaskIntersectionOverUnion=$masks[4];WithinOnePixelSavedCoverage=$masks[5];WithinOnePixelReopenCoverage=$masks[6];MeanAbsoluteRGBDifference=$comparison[4]}
}
$report=[pscustomobject]@{Scope='Actual exported viewport pixels; color area/width response and persistence only, not full rendering acceptance';Rows=$rows;Checks=$checks;Passed=(@($checks|Where-Object {-not $_.Passed}).Count -eq 0)}
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskRoot 'PixelMetrics.json') -Encoding utf8
$rows | Format-Table -AutoSize
$checks | Format-Table -AutoSize
if(-not $report.Passed){throw 'One or more Outline image checks failed. Inspect actual captures.'}
