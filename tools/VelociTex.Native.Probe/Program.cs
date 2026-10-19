using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using VelociTex.Rendering.Direct2D;
using VelociTex;

string output=Path.GetFullPath(args.Length>0?args[0]:"artifacts/native/jit");
string fonts=Path.GetFullPath(args.Length>1?args[1]:Path.Combine(AppContext.BaseDirectory,"Fonts"));
Directory.CreateDirectory(output);
long allocatedBefore=GC.GetTotalAllocatedBytes(true);
Stopwatch timer=Stopwatch.StartNew();
List<(string Name,string Hash,long Bytes)> files=[];
List<NativeFontProbeResult> nativeFonts=[];
using(NativeProbeRenderer renderer=new(fonts))
{
    foreach(FontDescriptor font in FontData.Fonts)
    {
        uint[] codePoints=new uint[font.GlyphCount];ushort[] glyphIds=new ushort[font.GlyphCount];
        for(int i=0;i<font.GlyphCount;i++){FontGlyph item=FontData.Glyphs[font.GlyphStart+i];codePoints[i]=item.CodePoint;glyphIds[i]=item.GlyphId;}
        NativeFontProbeResult result=renderer.CheckPrivateFont(Path.Combine(fonts,font.ResourceFileName),codePoints,glyphIds);
        nativeFonts.Add(result);
        Console.WriteLine($"FONT {result.FileName}: glyphs={result.GlyphCount}; cmap={result.CodePointCount}; missing={result.MissingCodePoints.Length}; mismatches={result.MismatchedCodePoints.Length}");
    }
    foreach(bool warp in new[]{false,true})
    foreach(ProbeTheme theme in Enum.GetValues<ProbeTheme>())
    foreach(int scale in new[]{100,125,150,200})
    {
        string name=$"probe-{(warp?"warp":"wic")}-{theme.ToString().ToLowerInvariant()}-{scale}.png";
        renderer.WritePng(Path.Combine(output,name),scale,theme,warp);
        Console.WriteLine($"PASS {name}");
        Track(name);
    }
    foreach(ProbeTheme theme in Enum.GetValues<ProbeTheme>())
    {
        string name=$"probe-{theme.ToString().ToLowerInvariant()}.svg";
        renderer.WriteSvg(Path.Combine(output,name),theme);Track(name);
    }
}
// New renderer plus fresh WARP device and resources on each export proves owned-resource reconstruction.
string stable=files.First(f=>f.Name=="probe-warp-light-100.png").Hash;
for(int cycle=0;cycle<5;cycle++)
{
    using NativeProbeRenderer renderer=new(fonts);
    string path=Path.Combine(output,"reload.png");renderer.WritePng(path,warp:true);
    string hash=Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path)));
    if(hash!=stable)throw new InvalidOperationException("Reloaded renderer output differs.");
}
File.Delete(Path.Combine(output,"reload.png"));
NativeInlineBaselineProbe.WriteReport(Path.Combine(output,"inline-baselines.json"));
InlineParagraphMetrics wide=NativeInlineBaselineProbe.MeasureParagraph(780),narrow=NativeInlineBaselineProbe.MeasureParagraph(280);
if(wide.Height!=74||wide.FirstBaseline!=43||wide.LineCount!=1||narrow.Height!=134||narrow.FirstBaseline!=42||narrow.LineCount!=2)
    throw new InvalidOperationException("Paragraph measurement differs from its callback report.");
Console.WriteLine("PASS DirectWrite inline-object callbacks and wide/narrow baseline layout");
timer.Stop();
using(FileStream file=File.Create(Path.Combine(output,"report.json")))
using(Utf8JsonWriter json=new(file,new JsonWriterOptions{Indented=true}))
{
    json.WriteStartObject();json.WriteString("kind","P0 hard-coded feasibility probe; not formula compatibility");
    json.WriteString("utc",DateTimeOffset.UtcNow);json.WriteString("runtime",RuntimeInformation.FrameworkDescription);
    json.WriteString("architecture",RuntimeInformation.ProcessArchitecture.ToString());json.WriteString("os",RuntimeInformation.OSDescription);
    json.WriteBoolean("dynamicCodeSupported",System.Runtime.CompilerServices.RuntimeFeature.IsDynamicCodeSupported);
    json.WriteNumber("elapsedMs",timer.Elapsed.TotalMilliseconds);json.WriteNumber("managedAllocatedBytesIncludingResultsAndExports",GC.GetTotalAllocatedBytes(true)-allocatedBefore);
    json.WriteNumber("processPrivateBytes",Process.GetCurrentProcess().PrivateMemorySize64);
    json.WriteNumber("logicalWidthDip",NativeProbeRenderer.Width);json.WriteNumber("logicalHeightDip",NativeProbeRenderer.Height);json.WriteNumber("baselineDip",NativeProbeRenderer.Baseline);
    json.WriteNumber("unloadReloadCycles",5);json.WriteBoolean("nativeResourcesRecreatedEachWarpExport",true);
    json.WriteStartArray("privateFonts");
    foreach(NativeFontProbeResult font in nativeFonts)
    {
        json.WriteStartObject();json.WriteString("file",font.FileName);json.WriteNumber("nativeGlyphCount",font.GlyphCount);json.WriteNumber("verifiedCodePoints",font.CodePointCount);
        json.WriteStartArray("missingCodePoints");foreach(uint point in font.MissingCodePoints)json.WriteNumberValue(point);json.WriteEndArray();
        json.WriteStartArray("mismatchedGlyphCodePoints");foreach(uint point in font.MismatchedCodePoints)json.WriteNumberValue(point);json.WriteEndArray();json.WriteEndObject();
    }
    json.WriteEndArray();
    json.WriteStartArray("outputs");foreach(var item in files){json.WriteStartObject();json.WriteString("name",item.Name);json.WriteString("sha256",item.Hash);json.WriteNumber("bytes",item.Bytes);json.WriteEndObject();}json.WriteEndArray();json.WriteEndObject();
}
if(nativeFonts.Count!=20||nativeFonts.Any(f=>f.MissingCodePoints.Length!=0||f.MismatchedCodePoints.Length!=0))
    throw new InvalidOperationException("One or more locked private fonts failed native DirectWrite mapping verification; see report.json.");
Console.WriteLine($"PASS outputs={files.Count}; unload/reload=5; elapsed={timer.Elapsed.TotalMilliseconds:F1}ms; AOT={!System.Runtime.CompilerServices.RuntimeFeature.IsDynamicCodeSupported}");
void Track(string name)
{
    byte[] bytes=File.ReadAllBytes(Path.Combine(output,name));files.Add((name,Convert.ToHexStringLower(SHA256.HashData(bytes)),bytes.Length));
}
