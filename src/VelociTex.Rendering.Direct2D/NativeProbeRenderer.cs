using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using static VelociTex.Rendering.Direct2D.NativeMethods;

namespace VelociTex.Rendering.Direct2D;

/// <summary>Colors for the explicitly positioned P0 feasibility drawing.</summary>
internal enum ProbeTheme
{
    /// <summary>Dark probe glyphs for a light background.</summary>
    Light,
    /// <summary>Light probe glyphs for a dark background.</summary>
    Dark
}

/// <summary>
/// Native feasibility probe only: positions are hard-coded and this class is not a formula engine.
/// Instances must be used and disposed on one thread; the caller owns target BeginDraw/EndDraw.
/// </summary>
internal sealed unsafe partial class NativeProbeRenderer : IDisposable
{
    /// <summary>Width of the hard-coded P0 verification chart in DIP.</summary>
    public const float Width = 640;
    /// <summary>Height of the hard-coded P0 verification chart in DIP.</summary>
    public const float Height = 180;
    /// <summary>Primary baseline of the hard-coded P0 verification chart in DIP.</summary>
    public const float Baseline = 112;
    private nint _writeFactory, _factory, _main, _italic;
    private readonly bool _uninitialize;
    private bool _disposed;
    private readonly Run[] _runs;
    private static readonly Rect[] Rules = [new(236, 94, 352, 96), new(426, 65, 485, 67)];
    private static readonly Point[] Radical = [new(394, 101), new(401, 96), new(410, 116), new(423, 65), new(429, 65), new(412, 126), new(407, 126), new(399, 104), new(395, 106)];

    /// <summary>Loads the fixed private fonts from a caller-owned directory for P0 verification.</summary>
    public NativeProbeRenderer(string fontDirectory)
    {
        int hr = CoInitializeEx(0, 0);
        if (hr < 0 && hr != unchecked((int)0x80010106)) Check(hr);
        _uninitialize = hr >= 0;
        try
        {
            Guid iid = new("b859ee5a-d838-4b5b-a2e8-1adc7d93db48");
            nint write = 0;
            Check(DWriteCreateFactory(1, &iid, &write)); _writeFactory = write;
            iid = new("06152247-6f50-465a-9245-118bfd3b6007");
            nint factory = 0;
            Check(D2D1CreateFactory(1, &iid, 0, &factory)); _factory = factory;
            _main = LoadFace(Path.Combine(fontDirectory, "KaTeX_Main-Regular.ttf"));
            _italic = LoadFace(Path.Combine(fontDirectory, "KaTeX_Math-Italic.ttf"));
            _runs = [
                MakeRun("x", true, 22,112,42), MakeRun("2",false,45,86,25),
                MakeRun("+",false,75,112,42), MakeRun("y",true,118,112,42), MakeRun("2",false,141,86,25),
                MakeRun("=",false,174,112,42),
                MakeRun("a",true,244,80,33),MakeRun("+",false,267,80,33),MakeRun("b",true,299,80,33),
                MakeRun("c",true,280,132,33), MakeRun("+",false,360,112,32),
                MakeRun("x",true,435,107,38),MakeRun("2",false,457,84,23),
                MakeRun("(",false,505,115,65),MakeRun("1 0",false,528,83,25),
                MakeRun("0 1",false,528,124,25),MakeRun(")",false,590,115,65)
            ];
        }
        catch { Dispose(); throw; }
    }

    /// <summary>Draws explicit glyph runs, rule rectangles and radical geometry to an ID2D1RenderTarget-compatible pointer.</summary>
    public void Draw(nint renderTarget, ProbeTheme theme = ProbeTheme.Light, float offsetX = 0, float offsetY = 0)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (renderTarget == 0 || !float.IsFinite(offsetX) || !float.IsFinite(offsetY)) throw new ArgumentOutOfRangeException(nameof(renderTarget));
        Color color = theme == ProbeTheme.Dark ? new(.94f,.95f,.98f) : new(.08f,.10f,.14f);
        nint brush = 0, targetFactory = 0, radical = 0;
        Check(((delegate* unmanaged[Stdcall]<nint, Color*, nint, nint*, int>)Slot(renderTarget,8))(renderTarget, &color, 0, &brush));
        Matrix original = default;
        ((delegate* unmanaged[Stdcall]<nint, Matrix*, void>)Slot(renderTarget,31))(renderTarget,&original);
        uint originalTextAntialias=((delegate* unmanaged[Stdcall]<nint,uint>)Slot(renderTarget,35))(renderTarget);
        Matrix transform = original;
        transform.Dx += offsetX; transform.Dy += offsetY;
        try
        {
            ((delegate* unmanaged[Stdcall]<nint,nint*,void>)Slot(renderTarget,3))(renderTarget,&targetFactory);
            radical=CreateRadical(targetFactory);
            ((delegate* unmanaged[Stdcall]<nint, Matrix*, void>)Slot(renderTarget,30))(renderTarget,&transform);
            ((delegate* unmanaged[Stdcall]<nint, uint, void>)Slot(renderTarget,34))(renderTarget,2); // grayscale on transparent surfaces
            foreach (Run run in _runs)
            {
                fixed (ushort* glyphs = run.Glyphs)
                fixed (float* advances = run.Advances)
                {
                    GlyphRun glyphRun = new() { FontFace=run.Italic?_italic:_main,EmSize=run.Size,GlyphCount=(uint)run.Glyphs.Length,GlyphIndices=glyphs,GlyphAdvances=advances };
                    ((delegate* unmanaged[Stdcall]<nint, Point, GlyphRun*, nint, uint, void>)Slot(renderTarget,29))(renderTarget,new(run.X,run.Y),&glyphRun,brush,0);
                }
            }
            foreach (Rect rule in Rules)
            {
                Rect rect = rule;
                ((delegate* unmanaged[Stdcall]<nint, Rect*, nint, void>)Slot(renderTarget,17))(renderTarget,&rect,brush);
            }
            ((delegate* unmanaged[Stdcall]<nint,nint,nint,nint,void>)Slot(renderTarget,23))(renderTarget,radical,brush,0);
        }
        finally
        {
            ((delegate* unmanaged[Stdcall]<nint, Matrix*, void>)Slot(renderTarget,30))(renderTarget,&original);
            ((delegate* unmanaged[Stdcall]<nint,uint,void>)Slot(renderTarget,34))(renderTarget,originalTextAntialias);
            Release(ref brush);
            Release(ref radical);Release(ref targetFactory);
        }
    }

    /// <summary>Creates an owned ID2D1Device backed by Direct3D WARP. Release the returned COM reference.</summary>
    public static nint CreateWarpD2DDevice()
    {
        nint d3d=0, context=0, dxgi=0, device=0;
        try
        {
            Check(D3D11CreateDevice(0,5,0,0x20,0,0,7,&d3d,null,&context));
            dxgi=Query(d3d,new("54ec77fa-1377-44e6-8c32-88fd5f44c84c"));
            Check(D2D1CreateDevice(dxgi,0,&device));
            return device;
        }
        finally { Release(ref dxgi); Release(ref context); Release(ref d3d); }
    }

    /// <summary>Checks a private face's native glyph count and cmap mapping against the locked generated data.</summary>
    public NativeFontProbeResult CheckPrivateFont(string path,ReadOnlySpan<uint> codePoints,ReadOnlySpan<ushort> expectedGlyphIds)
    {
        ObjectDisposedException.ThrowIf(_disposed,this);
        if(codePoints.Length!=expectedGlyphIds.Length)throw new ArgumentException("Code point and glyph ID counts must match.",nameof(expectedGlyphIds));
        nint face=0;
        try
        {
            face=LoadFace(path);
            ushort count=((delegate* unmanaged[Stdcall]<nint,ushort>)Slot(face,9))(face);
            if(count==0)throw new InvalidOperationException("The native private font contains no glyphs.");
            ushort[] glyphs=new ushort[codePoints.Length];
            fixed(uint* chars=codePoints)
            fixed(ushort* indices=glyphs)
                Check(((delegate* unmanaged[Stdcall]<nint,uint*,uint,ushort*,int>)Slot(face,11))(face,chars,(uint)codePoints.Length,indices));
            List<uint> missing=[],mismatches=[];
            for(int i=0;i<glyphs.Length;i++)
            {
                if(glyphs[i]==0)missing.Add(codePoints[i]);
                if(glyphs[i]!=expectedGlyphIds[i]||glyphs[i]>=count)mismatches.Add(codePoints[i]);
            }
            return new(Path.GetFileName(path),count,codePoints.Length,missing.ToArray(),mismatches.ToArray());
        }
        finally{Release(ref face);}
    }

    internal void DrawInlineProbe(nint target,uint kind,float x,float y,nint brush)
    {
        Run[] runs=kind==1?
            [MakeRun("a",true,x+18,y+23,26),MakeRun("+",false,x+37,y+23,26),MakeRun("b",true,x+63,y+23,26),MakeRun("c",true,x+45,y+55,26)]:
            [MakeRun("(",false,x+2,y+57,64),MakeRun("1 0",false,x+28,y+27,22),MakeRun("0 1",false,x+28,y+61,22),MakeRun(")",false,x+85,y+57,64)];
        foreach(Run run in runs)
        {
            fixed(ushort* glyphs=run.Glyphs)
            fixed(float* advances=run.Advances)
            {
                GlyphRun value=new(){FontFace=run.Italic?_italic:_main,EmSize=run.Size,GlyphCount=(uint)run.Glyphs.Length,GlyphIndices=glyphs,GlyphAdvances=advances};
                ((delegate* unmanaged[Stdcall]<nint,Point,GlyphRun*,nint,uint,void>)Slot(target,29))(target,new(run.X,run.Y),&value,brush,0);
            }
        }
        if(kind==1)
        {
            Rect rect=new(x+12,y+31,x+94,y+32.5f);
            ((delegate* unmanaged[Stdcall]<nint,Rect*,nint,void>)Slot(target,17))(target,&rect,brush);
        }
    }

    /// <summary>Exports a transparent PNG via native WIC, or via a WARP bitmap copied to WIC.</summary>
    public void WritePng(string path, int scalePercent = 100, ProbeTheme theme = ProbeTheme.Light, bool warp = false)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (scalePercent is not (100 or 125 or 150 or 200)) throw new ArgumentOutOfRangeException(nameof(scalePercent));
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        uint width=(uint)(Width*scalePercent/100),height=(uint)(Height*scalePercent/100);
        float dpi=96f*scalePercent/100;
        nint wic=0,bitmap=0,target=0;
        Guid clsid=new("cacaf262-9370-4615-a13b-9f5539da4c0a"), iid=new("ec5ec8a9-c395-4314-9c77-54d7a935ff70");
        Guid pixel=new("6fddc324-4e03-4bfe-b185-3d77768dc910"); // 32bppPBGRA
        try
        {
            Check(CoCreateInstance(&clsid,0,1,&iid,&wic));
            if (warp)
            {
                byte[] pixels=RenderWarp(width,height,dpi,theme);
                fixed(byte* data=pixels)
                    Check(((delegate* unmanaged[Stdcall]<nint,uint,uint,Guid*,uint,uint,byte*,nint*,int>)Slot(wic,20))(wic,width,height,&pixel,width*4,(uint)pixels.Length,data,&bitmap));
            }
            else
            {
                Check(((delegate* unmanaged[Stdcall]<nint,uint,uint,Guid*,uint,nint*,int>)Slot(wic,17))(wic,width,height,&pixel,1,&bitmap));
                TargetProperties properties=new() {Type=1,PixelFormat=new(87,1),DpiX=dpi,DpiY=dpi};
                Check(((delegate* unmanaged[Stdcall]<nint,nint,TargetProperties*,nint*,int>)Slot(_factory,13))(_factory,bitmap,&properties,&target));
                BeginDraw(target); Clear(target); Draw(target,theme); EndDraw(target);
            }
            EncodePng(wic,bitmap,path,dpi);
        }
        finally { Release(ref target);Release(ref bitmap);Release(ref wic); }
    }

    private byte[] RenderWarp(uint width,uint height,float dpi,ProbeTheme theme)
    {
        nint device=0,context=0,bitmap=0,staging=0;
        try
        {
            device=CreateWarpD2DDevice();
            Check(((delegate* unmanaged[Stdcall]<nint,uint,nint*,int>)Slot(device,4))(device,0,&context));
            BitmapProperties properties=new() {PixelFormat=new(87,1),DpiX=dpi,DpiY=dpi,Options=1};
            Check(((delegate* unmanaged[Stdcall]<nint,SizeU,nint,uint,BitmapProperties*,nint*,int>)Slot(context,57))(context,new(width,height),0,0,&properties,&bitmap));
            ((delegate* unmanaged[Stdcall]<nint,nint,void>)Slot(context,74))(context,bitmap);
            SetDpi(context,dpi);
            BeginDraw(context);Clear(context);Draw(context,theme);EndDraw(context);
            ((delegate* unmanaged[Stdcall]<nint,nint,void>)Slot(context,74))(context,0);
            properties.Options=6;
            Check(((delegate* unmanaged[Stdcall]<nint,SizeU,nint,uint,BitmapProperties*,nint*,int>)Slot(context,57))(context,new(width,height),0,0,&properties,&staging));
            Check(((delegate* unmanaged[Stdcall]<nint,nint,nint,nint,int>)Slot(staging,8))(staging,0,bitmap,0));
            MappedRect mapped=default;
            Check(((delegate* unmanaged[Stdcall]<nint,uint,MappedRect*,int>)Slot(staging,14))(staging,1,&mapped));
            try
            {
                byte[] bytes=new byte[checked(width*height*4)];
                for(uint y=0;y<height;y++) new ReadOnlySpan<byte>(mapped.Bits+y*mapped.Pitch,(int)width*4).CopyTo(bytes.AsSpan((int)(y*width*4)));
                return bytes;
            }
            finally { Check(((delegate* unmanaged[Stdcall]<nint,int>)Slot(staging,15))(staging)); }
        }
        finally {Release(ref staging);Release(ref bitmap);Release(ref context);Release(ref device);}
    }

    private static void EncodePng(nint wic,nint bitmap,string path,float dpi)
    {
        nint stream=0,encoder=0,frame=0,options=0;
        try
        {
            Check(((delegate* unmanaged[Stdcall]<nint,nint*,int>)Slot(wic,14))(wic,&stream));
            fixed(char* name=path) Check(((delegate* unmanaged[Stdcall]<nint,char*,uint,int>)Slot(stream,15))(stream,name,0x40000000));
            Guid format=new("1b7cfaf4-713f-473c-bbcd-6137425faeaf");
            Check(((delegate* unmanaged[Stdcall]<nint,Guid*,nint,nint*,int>)Slot(wic,8))(wic,&format,0,&encoder));
            Check(((delegate* unmanaged[Stdcall]<nint,nint,uint,int>)Slot(encoder,3))(encoder,stream,2));
            Check(((delegate* unmanaged[Stdcall]<nint,nint*,nint*,int>)Slot(encoder,10))(encoder,&frame,&options));
            Check(((delegate* unmanaged[Stdcall]<nint,nint,int>)Slot(frame,3))(frame,options));
            Check(((delegate* unmanaged[Stdcall]<nint,double,double,int>)Slot(frame,5))(frame,dpi,dpi));
            Check(((delegate* unmanaged[Stdcall]<nint,nint,nint,int>)Slot(frame,11))(frame,bitmap,0));
            Check(((delegate* unmanaged[Stdcall]<nint,int>)Slot(frame,12))(frame));
            Check(((delegate* unmanaged[Stdcall]<nint,int>)Slot(encoder,11))(encoder));
        }
        finally {Release(ref options);Release(ref frame);Release(ref encoder);Release(ref stream);}
    }

    private nint LoadFace(string path)
    {
        if(!File.Exists(path)) throw new FileNotFoundException("The locked KaTeX private font is required.",path);
        nint file=0,face=0;
        try
        {
            fixed(char* name=Path.GetFullPath(path)) Check(((delegate* unmanaged[Stdcall]<nint,char*,nint,nint*,int>)Slot(_writeFactory,7))(_writeFactory,name,0,&file));
            Check(((delegate* unmanaged[Stdcall]<nint,uint,uint,nint*,uint,uint,nint*,int>)Slot(_writeFactory,9))(_writeFactory,1,1,&file,0,0,&face));
            return face;
        }
        finally {Release(ref file);}
    }

    private Run MakeRun(string text,bool italic,float x,float y,float size)
    {
        uint[] codepoints=text.EnumerateRunes().Select(r=>(uint)r.Value).ToArray();
        ushort[] glyphs=new ushort[codepoints.Length];
        float[] advances=new float[glyphs.Length];
        nint face=italic?_italic:_main;
        fixed(uint* chars=codepoints)
        fixed(ushort* indices=glyphs)
        {
            Check(((delegate* unmanaged[Stdcall]<nint,uint*,uint,ushort*,int>)Slot(face,11))(face,chars,(uint)glyphs.Length,indices));
            GlyphMetrics[] metrics=new GlyphMetrics[glyphs.Length];
            fixed(GlyphMetrics* values=metrics)
                Check(((delegate* unmanaged[Stdcall]<nint,ushort*,uint,GlyphMetrics*,int,int>)Slot(face,10))(face,indices,(uint)glyphs.Length,values,0));
            ushort* fontMetrics=stackalloc ushort[10];
            ((delegate* unmanaged[Stdcall]<nint,ushort*,void>)Slot(face,8))(face,fontMetrics);
            for(int i=0;i<glyphs.Length;i++) {if(glyphs[i]==0) throw new InvalidOperationException($"Missing private glyph U+{codepoints[i]:X}."); advances[i]=metrics[i].AdvanceWidth*size/fontMetrics[0];}
        }
        return new(text,italic,x,y,size,glyphs,advances);
    }

    private static nint CreateRadical(nint factory)
    {
        nint geometry=0,sink=0;
        try
        {
            Check(((delegate* unmanaged[Stdcall]<nint,nint*,int>)Slot(factory,10))(factory,&geometry));
            Check(((delegate* unmanaged[Stdcall]<nint,nint*,int>)Slot(geometry,17))(geometry,&sink));
            ((delegate* unmanaged[Stdcall]<nint,Point,uint,void>)Slot(sink,5))(sink,Radical[0],0);
            fixed(Point* points=Radical) ((delegate* unmanaged[Stdcall]<nint,Point*,uint,void>)Slot(sink,6))(sink,points+1,(uint)Radical.Length-1);
            ((delegate* unmanaged[Stdcall]<nint,uint,void>)Slot(sink,8))(sink,1);
            Check(((delegate* unmanaged[Stdcall]<nint,int>)Slot(sink,9))(sink));
            return geometry;
        }
        catch {Release(ref geometry);throw;}
        finally {Release(ref sink);}
    }

    /// <summary>Releases the P0 verification faces and native factories on the owning thread.</summary>
    public void Dispose()
    {
        if(_disposed)return;
        _disposed=true;
        Release(ref _italic);Release(ref _main);Release(ref _factory);Release(ref _writeFactory);
        if(_uninitialize)CoUninitialize();
    }
    private sealed record Run(string Text,bool Italic,float X,float Y,float Size,ushort[] Glyphs,float[] Advances);
}

/// <summary>Observed native private-font face mapping, used only for P0 verification.</summary>
internal readonly record struct NativeFontProbeResult(string FileName,ushort GlyphCount,int CodePointCount,uint[] MissingCodePoints,uint[] MismatchedCodePoints);
