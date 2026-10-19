using System.Globalization;
using System.Runtime.InteropServices;
using VelociTex;
using static VelociTex.Rendering.Direct2D.NativeMethods;

namespace VelociTex.Rendering.Direct2D;

/// <summary>A finite sRGB foreground color. Explicit formula colors are a later compatibility increment.</summary>
/// <param name="R">Red component between zero and one.</param>
/// <param name="G">Green component between zero and one.</param>
/// <param name="B">Blue component between zero and one.</param>
/// <param name="A">Opacity between zero and one; the default is fully opaque.</param>
public readonly record struct MathDrawingColor(float R,float G,float B,float A=1)
{
    /// <summary>Gets the standard dark foreground for a light background.</summary>
    public static MathDrawingColor Black => new(.08f,.10f,.14f);
    /// <summary>Gets the standard light foreground for a dark background.</summary>
    public static MathDrawingColor White => new(.94f,.95f,.98f);
    internal void Validate()
    {
        if(!float.IsFinite(R)||!float.IsFinite(G)||!float.IsFinite(B)||!float.IsFinite(A)||R<0||G<0||B<0||A<0||R>1||G>1||B>1||A>1)throw new ArgumentOutOfRangeException(nameof(MathDrawingColor));
    }
}

/// <summary>Owns the preparation lifetime of one immutable Core document. Use on the renderer's thread.</summary>
public sealed class PreparedMath : IDisposable
{
    internal WindowsMathRenderer Owner {get;}
    internal bool IsDisposed {get;private set;}
    /// <summary>Gets the independently owned document retained by this preparation.</summary>
    public MathDocument Document {get;}
    internal PreparedMath(WindowsMathRenderer owner,MathDocument document){Owner=owner;Document=document;}
    /// <summary>Releases this preparation on its owner's thread. The managed document remains valid.</summary>
    public void Dispose(){if(IsDisposed)return;Owner.VerifyDisposalThread();IsDisposed=true;}
}

/// <summary>Draws real Core drawing data through system DirectWrite/Direct2D. Owners and results are thread confined.</summary>
public sealed unsafe partial class WindowsMathRenderer : IDisposable
{
    private readonly int _threadId=Environment.CurrentManagedThreadId;
    private readonly bool _uninitialize;
    private readonly string _fontDirectory;
    private readonly bool _ownsFontDirectory;
    private readonly nint[] _faces=new nint[MathFontCatalog.Count];
    private nint _writeFactory,_factory,_renderingParams;
    private bool _disposed;
    private readonly Dictionary<(int Font,ushort Glyph,double Size),string> _outlines=[];
    private readonly Dictionary<(int Font,ushort Glyph,double Size),MathPathSegment[]> _drawingOutlines=[];
    private long _drawingOutlineBytes;
    private const int OutlineCacheLimit=4096;
    private const long OutlineCacheByteLimit=16*1024*1024;
    private long _outlineBytes;

    /// <summary>Creates an owned WARP-backed ID2D1Device reference for a Composition host.</summary>
    /// <returns>A native COM reference. The caller must release it with Marshal.Release.</returns>
    public static nint CreateWarpD2DDevice()=>NativeProbeRenderer.CreateWarpD2DDevice();

    /// <summary>Uses a private directory, or extracts unchanged embedded Core font bytes into an owned temporary directory.</summary>
    /// <param name="fontDirectory">Caller-owned private font directory, or null to extract and later delete the unchanged embedded fonts.</param>
    public WindowsMathRenderer(string? fontDirectory=null)
    {
        int hr=CoInitializeEx(0,0);
        if(hr<0&&hr!=unchecked((int)0x80010106))Check(hr);
        _uninitialize=hr>=0;
        _ownsFontDirectory=fontDirectory is null;
        _fontDirectory=fontDirectory is null?Path.Combine(Path.GetTempPath(),"VelociTex",Guid.NewGuid().ToString("N")):Path.GetFullPath(fontDirectory);
        try
        {
            if(_ownsFontDirectory)
            {
                Directory.CreateDirectory(_fontDirectory);
                for(int id=0;id<MathFontCatalog.Count;id++)
                {
                    using Stream source=MathFontCatalog.OpenFont(id);
                    using FileStream target=File.Create(Path.Combine(_fontDirectory,MathFontCatalog.GetResourceFileName(id)));
                    source.CopyTo(target);
                }
            }
            Guid iid=new("b859ee5a-d838-4b5b-a2e8-1adc7d93db48");nint write=0;
            Check(DWriteCreateFactory(1,&iid,&write));_writeFactory=write;
            nint rendering=0;
            // Outline rendering preserves the exact retained font geometry at
            // fractional origins, matching self-contained SVG paths. Font
            // grid fitting otherwise distorts small heavy glyph contours.
            Check(((delegate* unmanaged[Stdcall]<nint,float,float,float,uint,uint,nint*,int>)Slot(write,12))(write,2.2f,0,0,0,6,&rendering));_renderingParams=rendering;
            iid=new("06152247-6f50-465a-9245-118bfd3b6007");nint factory=0;
            Check(D2D1CreateFactory(1,&iid,0,&factory));_factory=factory;
        }
        catch{Dispose();throw;}
    }

    /// <summary>Validates private-font glyphs and retains the document for native drawing.</summary>
    /// <param name="document">An independently owned compilation result.</param>
    /// <returns>A disposable preparation usable only with this renderer on its creating thread.</returns>
    public PreparedMath Prepare(MathDocument document)
    {
        CheckThread();ArgumentNullException.ThrowIfNull(document);
        foreach(MathGlyph glyph in document.Glyphs.Span)
        {
            nint face=GetFace(glyph.FontId);
            ushort count=((delegate* unmanaged[Stdcall]<nint,ushort>)Slot(face,9))(face);
            if(glyph.GlyphId==0||glyph.GlyphId>=count)throw new InvalidOperationException("The document references a missing private-font glyph.");
        }
        return new(this,document);
    }

    /// <summary>The caller owns target BeginDraw/EndDraw. Glyph positions and paths come solely from the prepared Core document.</summary>
    /// <param name="prepared">A live preparation created by this renderer.</param>
    /// <param name="renderTarget">A caller-owned ID2D1RenderTarget or device-context pointer.</param>
    /// <param name="color">A finite sRGB foreground.</param>
    /// <param name="offsetX">Horizontal translation in DIP.</param>
    /// <param name="offsetY">Vertical translation in DIP.</param>
    public void Draw(PreparedMath prepared,nint renderTarget,MathDrawingColor color,double offsetX=0,double offsetY=0)
    {
        CheckPrepared(prepared);color.Validate();
        if(renderTarget==0||!double.IsFinite(offsetX)||!double.IsFinite(offsetY))throw new ArgumentOutOfRangeException(nameof(renderTarget));
        Color nativeColor=new(color.R,color.G,color.B,color.A);nint brush=0,factory=0;
        Check(((delegate* unmanaged[Stdcall]<nint,Color*,nint,nint*,int>)Slot(renderTarget,8))(renderTarget,&nativeColor,0,&brush));
        Matrix original=default;
        ((delegate* unmanaged[Stdcall]<nint,Matrix*,void>)Slot(renderTarget,31))(renderTarget,&original);
        uint originalAa=((delegate* unmanaged[Stdcall]<nint,uint>)Slot(renderTarget,35))(renderTarget);
        nint originalRendering=0;
        ((delegate* unmanaged[Stdcall]<nint,nint*,void>)Slot(renderTarget,37))(renderTarget,&originalRendering);
        Matrix translated=original;translated.Dx+=(float)offsetX;translated.Dy+=(float)offsetY;
        try
        {
            ((delegate* unmanaged[Stdcall]<nint,nint*,void>)Slot(renderTarget,3))(renderTarget,&factory);
            ((delegate* unmanaged[Stdcall]<nint,Matrix*,void>)Slot(renderTarget,30))(renderTarget,&translated);
            ((delegate* unmanaged[Stdcall]<nint,uint,void>)Slot(renderTarget,34))(renderTarget,2);
            ((delegate* unmanaged[Stdcall]<nint,nint,void>)Slot(renderTarget,36))(renderTarget,_renderingParams);
            if(prepared.Document.PaintStates.Length!=0)
            {
                int previousState=-1;Matrix stateTransform=translated;
                foreach(MathPaintCommand command in prepared.Document.PaintCommands.Span)
                {
                    MathPaintState state=prepared.Document.PaintStates.Span[command.StateIndex];
                    if(state.Color is {A:0})continue;
                    if(previousState!=command.StateIndex)
                    {
                        stateTransform=Compose(state.Transform,translated);
                        Color ink=state.Color is {} explicitColor?new(explicitColor.R/255f,explicitColor.G/255f,explicitColor.B/255f,explicitColor.A/255f):nativeColor;
                        ((delegate* unmanaged[Stdcall]<nint,Color*,void>)Slot(brush,8))(brush,&ink);
                        previousState=command.StateIndex;
                    }
                    ((delegate* unmanaged[Stdcall]<nint,Matrix*,void>)Slot(renderTarget,30))(renderTarget,&stateTransform);
                    switch(command.Kind)
                    {
                        case MathPaintKind.Glyph:DrawGlyph(prepared.Document.Glyphs.Span[command.Index],brush,factory);break;
                        case MathPaintKind.Rule:DrawRule(prepared.Document.Rules.Span[command.Index],brush);break;
                        case MathPaintKind.Path:DrawPath(renderTarget,factory,brush,prepared.Document.Paths.Span[command.Index],stateTransform);break;
                        default:throw new InvalidOperationException("Unknown retained painting command.");
                    }
                }
                return;
            }
            foreach(MathGlyph glyph in prepared.Document.Glyphs.Span)
            {
                DrawGlyph(glyph,brush,factory);
            }
            foreach(MathRule rule in prepared.Document.Rules.Span)
            {
                DrawRule(rule,brush);
            }
            if(prepared.Document.Paths.Length!=0)
            {
                foreach(MathPath path in prepared.Document.Paths.Span)DrawPath(renderTarget,factory,brush,path,translated);
            }
        }
        finally
        {
            ((delegate* unmanaged[Stdcall]<nint,Matrix*,void>)Slot(renderTarget,30))(renderTarget,&original);
            ((delegate* unmanaged[Stdcall]<nint,uint,void>)Slot(renderTarget,34))(renderTarget,originalAa);
            ((delegate* unmanaged[Stdcall]<nint,nint,void>)Slot(renderTarget,36))(renderTarget,originalRendering);
            Release(ref originalRendering);Release(ref factory);Release(ref brush);
        }
        void DrawGlyph(MathGlyph glyph,nint drawBrush,nint drawFactory)
        {
            var key=(glyph.FontId,glyph.GlyphId,glyph.FontSize);
            if(!_drawingOutlines.TryGetValue(key,out var outline))
            {
                outline=NativeGlyphOutline.GetSegments(GetFace(glyph.FontId),glyph.GlyphId,(float)glyph.FontSize);
                long bytes=outline.Length*64L+128;
                if(_drawingOutlines.Count+_outlines.Count>=OutlineCacheLimit||_drawingOutlineBytes+_outlineBytes+bytes>OutlineCacheByteLimit)ClearOutlineCaches();
                if(bytes<=OutlineCacheByteLimit){_drawingOutlines.Add(key,outline);_drawingOutlineBytes+=bytes;}
            }
            if(outline.Length==0)return;
            Matrix matrix=default;((delegate* unmanaged[Stdcall]<nint,Matrix*,void>)Slot(renderTarget,31))(renderTarget,&matrix);
            DrawPath(renderTarget,drawFactory,drawBrush,new(outline,glyph.X,glyph.Y,1,1,new(-32768,-32768,65536,65536)),matrix);
        }
        void DrawRule(MathRule rule,nint drawBrush)
        {
            Rect rect=new((float)rule.X,(float)rule.Y,(float)(rule.X+rule.Width),(float)(rule.Y+rule.Height));
            ((delegate* unmanaged[Stdcall]<nint,Rect*,nint,void>)Slot(renderTarget,17))(renderTarget,&rect,drawBrush);
        }
    }

    private static Matrix Compose(MathTransform a,Matrix b)=>new()
    {
        M11=(float)(a.M11*b.M11+a.M12*b.M21),M12=(float)(a.M11*b.M12+a.M12*b.M22),
        M21=(float)(a.M21*b.M11+a.M22*b.M21),M22=(float)(a.M21*b.M12+a.M22*b.M22),
        Dx=(float)(a.Dx*b.M11+a.Dy*b.M21+b.Dx),Dy=(float)(a.Dx*b.M12+a.Dy*b.M22+b.Dy)
    };

    private void ClearOutlineCaches(){_outlines.Clear();_drawingOutlines.Clear();_outlineBytes=_drawingOutlineBytes=0;}

    private static void DrawPath(nint target,nint factory,nint brush,MathPath path,Matrix original)
    {
        nint geometry=0,sink=0;bool clipped=false;
        try
        {
            Check(((delegate* unmanaged[Stdcall]<nint,nint*,int>)Slot(factory,10))(factory,&geometry));
            Check(((delegate* unmanaged[Stdcall]<nint,nint*,int>)Slot(geometry,17))(geometry,&sink));
            // Upstream SVG geometry uses nonzero winding; overlapping radical bars cancel under D2D's alternate default.
            ((delegate* unmanaged[Stdcall]<nint,uint,void>)Slot(sink,3))(sink,1);
            bool open=false;
            foreach(MathPathSegment segment in path.Segments.Span)
            {
                switch(segment.Operation)
                {
                    case MathPathOperation.Move:
                        if(open)((delegate* unmanaged[Stdcall]<nint,uint,void>)Slot(sink,8))(sink,0);
                        ((delegate* unmanaged[Stdcall]<nint,Point,uint,void>)Slot(sink,5))(sink,new((float)segment.X1,(float)segment.Y1),0);open=true;break;
                    case MathPathOperation.Line:
                        Point point=new((float)segment.X1,(float)segment.Y1);
                        ((delegate* unmanaged[Stdcall]<nint,Point*,uint,void>)Slot(sink,6))(sink,&point,1);break;
                    case MathPathOperation.Cubic:
                        Bezier curve=new(){P1=new((float)segment.X1,(float)segment.Y1),P2=new((float)segment.X2,(float)segment.Y2),P3=new((float)segment.X3,(float)segment.Y3)};
                        ((delegate* unmanaged[Stdcall]<nint,Bezier*,uint,void>)Slot(sink,7))(sink,&curve,1);break;
                    case MathPathOperation.Close:
                        ((delegate* unmanaged[Stdcall]<nint,uint,void>)Slot(sink,8))(sink,1);open=false;break;
                }
            }
            if(open)((delegate* unmanaged[Stdcall]<nint,uint,void>)Slot(sink,8))(sink,0);
            Check(((delegate* unmanaged[Stdcall]<nint,int>)Slot(sink,9))(sink));
            Rect clip=new((float)path.ClipBounds.X,(float)path.ClipBounds.Y,(float)(path.ClipBounds.X+path.ClipBounds.Width),(float)(path.ClipBounds.Y+path.ClipBounds.Height));
            ((delegate* unmanaged[Stdcall]<nint,Rect*,uint,void>)Slot(target,45))(target,&clip,0);clipped=true;
            Matrix transform=original;
            transform.M11=(float)path.ScaleX*original.M11;transform.M12=(float)path.ScaleX*original.M12;
            transform.M21=(float)path.ScaleY*original.M21;transform.M22=(float)path.ScaleY*original.M22;
            transform.Dx=(float)path.X*original.M11+(float)path.Y*original.M21+original.Dx;
            transform.Dy=(float)path.X*original.M12+(float)path.Y*original.M22+original.Dy;
            ((delegate* unmanaged[Stdcall]<nint,Matrix*,void>)Slot(target,30))(target,&transform);
            ((delegate* unmanaged[Stdcall]<nint,nint,nint,nint,void>)Slot(target,23))(target,geometry,brush,0);
        }
        finally
        {
            ((delegate* unmanaged[Stdcall]<nint,Matrix*,void>)Slot(target,30))(target,&original);
            if(clipped)((delegate* unmanaged[Stdcall]<nint,void>)Slot(target,46))(target);
            Release(ref sink);Release(ref geometry);
        }
    }

    private nint GetFace(int fontId)
    {
        if((uint)fontId>=(uint)_faces.Length)throw new ArgumentOutOfRangeException(nameof(fontId));
        if(_faces[fontId]!=0)return _faces[fontId];
        string path=Path.Combine(_fontDirectory,MathFontCatalog.GetResourceFileName(fontId));nint file=0,face=0;
        try
        {
            fixed(char* name=path)Check(((delegate* unmanaged[Stdcall]<nint,char*,nint,nint*,int>)Slot(_writeFactory,7))(_writeFactory,name,0,&file));
            Check(((delegate* unmanaged[Stdcall]<nint,uint,uint,nint*,uint,uint,nint*,int>)Slot(_writeFactory,9))(_writeFactory,1,1,&file,0,0,&face));
            return _faces[fontId]=face;
        }
        finally{Release(ref file);}
    }
    private void CheckThread(){ObjectDisposedException.ThrowIf(_disposed,this);if(Environment.CurrentManagedThreadId!=_threadId)throw new InvalidOperationException("Native math resources must be used on their creating thread.");}
    internal void VerifyDisposalThread(){if(Environment.CurrentManagedThreadId!=_threadId)throw new InvalidOperationException("Dispose native math preparations on their creating thread.");}
    private void CheckPrepared(PreparedMath prepared){CheckThread();ArgumentNullException.ThrowIfNull(prepared);ObjectDisposedException.ThrowIf(prepared.IsDisposed,prepared);if(!ReferenceEquals(prepared.Owner,this))throw new ArgumentException("The prepared result belongs to another renderer.",nameof(prepared));}
    /// <summary>Releases native fonts, factories and cached outlines, then removes any owned temporary font directory.</summary>
    /// <exception cref="InvalidOperationException">Called from a thread other than the creating thread.</exception>
    public void Dispose()
    {
        if(_disposed)return;
        if(Environment.CurrentManagedThreadId!=_threadId)throw new InvalidOperationException("Dispose native math resources on their creating thread.");
        _disposed=true;for(int i=0;i<_faces.Length;i++)Release(ref _faces[i]);Release(ref _renderingParams);Release(ref _factory);Release(ref _writeFactory);_outlines.Clear();_outlineBytes=0;_drawingOutlines.Clear();_drawingOutlineBytes=0;
        if(_uninitialize)CoUninitialize();
        if(_ownsFontDirectory&&Directory.Exists(_fontDirectory))Directory.Delete(_fontDirectory,true);
    }
    private static string Number(double value)=>value.ToString("0.########",CultureInfo.InvariantCulture);
}
