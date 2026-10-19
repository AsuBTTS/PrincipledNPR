param([string]$ProjectRoot = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
public static class MultiOutlineMetrics {
    public static int[] Read(string path) {
        using(var b=new Bitmap(path)) {
            int visible=0, red=0, green=0, middleRed=0;
            for(int y=0;y<b.Height;y++) for(int x=0;x<b.Width;x++) {
                var c=b.GetPixel(x,y);
                if(Math.Max(c.R,Math.Max(c.G,c.B))>30)visible++;
                bool r=c.R>80 && c.R>2*c.G && c.R>2*c.B;
                bool g=c.G>80 && c.G>2*c.R && c.G>2*c.B;
                if(r)red++; if(g)green++;
                if(r && Math.Abs(x-b.Width/2)<16 && Math.Abs(y-b.Height/2)<70)middleRed++;
            }
            return new[]{visible,red,green,middleRed};
        }
    }
}
'@
$taskFolder=Join-Path $ProjectRoot 'Documentation\Outline\MultiScope\Validation\Create'
$metrics=@{}
foreach($name in @('NoOutline','Unified','Independent','Occluded')) {
    $values=[MultiOutlineMetrics]::Read((Join-Path $taskFolder ($name+'.png')))
    $metrics[$name]=@{Visible=$values[0];Red=$values[1];Green=$values[2];CenterRed=$values[3]}
}
$checks=@(
    @{Name='NonBlankBaseline';Passed=($metrics.NoOutline.Visible -gt 1000)},
    @{Name='UnifiedVisibleRedOutline';Passed=($metrics.Unified.Red -gt 100 -and $metrics.Unified.Green -eq 0)},
    @{Name='IndependentGroupsBothRender';Passed=($metrics.Independent.Red -gt 100 -and $metrics.Independent.Green -gt 100)},
    @{Name='IndependentAddsBoundaryCoverage';Passed=(($metrics.Independent.Red+$metrics.Independent.Green) -gt $metrics.Unified.Red*1.05)},
    @{Name='ForegroundOccludesCenterLine';Passed=($metrics.Occluded.Red -gt 50 -and $metrics.Occluded.CenterRed -eq 0 -and $metrics.Occluded.Red -lt $metrics.Unified.Red)}
)
$report=@{Metrics=$metrics;Checks=$checks;Passed=(@($checks|Where-Object {-not $_.Passed}).Count -eq 0)}
$report|ConvertTo-Json -Depth 7|Set-Content (Join-Path $taskFolder 'PixelMetrics.json') -Encoding UTF8
$report|ConvertTo-Json -Depth 7
if(-not $report.Passed){throw 'MultiScope pixel checks failed; inspect raw captures.'}
