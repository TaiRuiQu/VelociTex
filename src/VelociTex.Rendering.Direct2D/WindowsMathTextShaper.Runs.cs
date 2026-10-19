using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using VelociTex;
using static VelociTex.Rendering.Direct2D.NativeMethods;

namespace VelociTex.Rendering.Direct2D;

public sealed unsafe partial class WindowsMathTextShaper
{
    /// <summary>Shapes complete text through DirectWrite's real contextual, fallback and bidirectional layout.</summary>
    /// <remarks>Run outlines are not cached. Native resources are released before return. All
    /// required layout and outline work is synchronous and belongs to formula compilation.</remarks>
    public bool TryShapeRun(string text,MathFont font,out MathTextRunShape shape)
    {
        ArgumentNullException.ThrowIfNull(text);shape=default;
        if(text.Length==0){shape=new(0,ReadOnlyMemory<MathPath>.Empty,new(0,0,0,0));return true;}
        if(text.Length>1_048_576)return false;
        lock(_gate)return ShapeRunNative(text,font,out shape);
    }
    private static bool ShapeRunNative(string text,MathFont font,out MathTextRunShape shape)
    {
        // HarfBuzz/browser shaping composes canonically equivalent Hangul and
        // combining sequences before glyph substitution. DirectWrite's layout
        // API expects that canonical normalization at its input boundary.
        // The compiler retains original source/text provenance independently.
        text=text.Normalize(System.Text.NormalizationForm.FormC);
        shape=default;int hr=CoInitializeEx(0,0);if(hr<0&&hr!=unchecked((int)0x80010106))Check(hr);
        bool uninitialize=hr>=0;nint factory=0,format=0,layout=0;GCHandle handle=default;
        try
        {
            Guid iid=new("b859ee5a-d838-4b5b-a2e8-1adc7d93db48");Check(DWriteCreateFactory(0,&iid,&factory));
            string family=font is MathFont.MathSans or MathFont.MathSansBold or MathFont.MathSansItalic or MathFont.MathSansBoldItalic?"Segoe UI":font==MathFont.MathTypewriter?"Consolas":"Cambria Math";
            // Generic browser fallback on the supported Windows baseline uses
            // these native script fonts. The entire run still passes through
            // DirectWrite, including substitutions, marks and bidi ordering.
            if(text.Any(c=>c is >= '\u0e00' and <= '\u0e7f'))family="Tahoma";
            else if(text.Any(c=>c is >= '\u1100' and <= '\u11ff' or >= '\ua960' and <= '\ua97f' or >= '\uac00' and <= '\ud7af' or >= '\ud7b0' and <= '\ud7ff'))family="Malgun Gothic";
            else if(text.Any(c=>c is >= '\u10a0' and <= '\u10ff'))family="Sylfaen";
            else if(text.Any(c=>c is >= '\u0590' and <= '\u08ff'))family="Times New Roman";
            uint weight=font is MathFont.MathBold or MathFont.MathBoldItalic or MathFont.MainBoldItalic or MathFont.MathSansBold or MathFont.MathSansBoldItalic?700u:400u;
            uint style=font is MathFont.MathItalic or MathFont.MathBoldItalic or MathFont.MainBoldItalic or MathFont.MathSansItalic or MathFont.MathSansBoldItalic?2u:0u;
            fixed(char* name=family)fixed(char* locale="en-US")
                Check(((delegate* unmanaged[Stdcall]<nint,char*,nint,uint,uint,uint,float,char*,nint*,int>)Slot(factory,15))(factory,name,0,weight,style,5,1,locale,&format));
            Check(((delegate* unmanaged[Stdcall]<nint,uint,int>)Slot(format,5))(format,1));
            fixed(char* source=text)
                Check(((delegate* unmanaged[Stdcall]<nint,char*,uint,nint,float,float,nint*,int>)Slot(factory,18))(factory,source,(uint)text.Length,format,2_000_000,4096,&layout));
            RunTextMetrics metrics=default;Check(((delegate* unmanaged[Stdcall]<nint,RunTextMetrics*,int>)Slot(layout,60))(layout,&metrics));
            RunLineMetrics line=default;uint count=0;int lineHr=((delegate* unmanaged[Stdcall]<nint,RunLineMetrics*,uint,uint*,int>)Slot(layout,59))(layout,&line,1,&count);
            if(lineHr<0||count!=1||!float.IsFinite(metrics.WidthIncludingTrailingWhitespace))return false;
            RunState state=new(){Baseline=line.Baseline};handle=GCHandle.Alloc(state);
            nint* vtable=stackalloc nint[10];
            vtable[0]=(nint)(delegate* unmanaged[Stdcall]<RunSink*,Guid*,nint*,int>)&RunQuery;
            vtable[1]=(nint)(delegate* unmanaged[Stdcall]<RunSink*,uint>)&RunAddRef;
            vtable[2]=(nint)(delegate* unmanaged[Stdcall]<RunSink*,uint>)&RunRelease;
            vtable[3]=(nint)(delegate* unmanaged[Stdcall]<RunSink*,nint,int*,int>)&RunPixelSnapping;
            vtable[4]=(nint)(delegate* unmanaged[Stdcall]<RunSink*,nint,Matrix*,int>)&RunTransform;
            vtable[5]=(nint)(delegate* unmanaged[Stdcall]<RunSink*,nint,float*,int>)&RunPixelsPerDip;
            vtable[6]=(nint)(delegate* unmanaged[Stdcall]<RunSink*,nint,float,float,uint,GlyphRun*,nint,nint,int>)&RunGlyphs;
            vtable[7]=(nint)(delegate* unmanaged[Stdcall]<RunSink*,nint,float,float,nint,nint,int>)&RunDecoration;
            vtable[8]=vtable[7];
            vtable[9]=(nint)(delegate* unmanaged[Stdcall]<RunSink*,nint,float,float,nint,int,int,nint,int>)&RunInline;
            RunSink sink=new(){Vtable=vtable,Handle=GCHandle.ToIntPtr(handle),References=1};
            Check(((delegate* unmanaged[Stdcall]<nint,nint,nint,float,float,int>)Slot(layout,58))(layout,0,(nint)(&sink),0,0));
            if(state.Error is not null)throw new InvalidOperationException("Contextual text outline callback failed.",state.Error);
            var bounds=double.IsPositiveInfinity(state.Left)?new MathBounds(0,0,0,0):new MathBounds(state.Left,state.Top,state.Right-state.Left,state.Bottom-state.Top);
            shape=new(metrics.WidthIncludingTrailingWhitespace,state.Paths.ToArray(),bounds);return true;
        }
        finally{Release(ref layout);if(handle.IsAllocated)handle.Free();Release(ref format);Release(ref factory);if(uninitialize)CoUninitialize();}
    }
    [StructLayout(LayoutKind.Sequential)]private struct RunTextMetrics{internal float Left,Top,Width,WidthIncludingTrailingWhitespace,Height,LayoutWidth,LayoutHeight;internal uint MaximumBidiReorderingDepth,LineCount;}
    [StructLayout(LayoutKind.Sequential)]private struct RunLineMetrics{internal uint Length,TrailingWhitespaceLength,NewlineLength;internal float Height,Baseline;internal int IsTrimmed;}
    [StructLayout(LayoutKind.Sequential)]private struct RunSink{internal nint* Vtable;internal nint Handle;internal uint References;}
    private sealed class RunState
    {
        internal readonly List<MathPath> Paths=[];internal float Baseline;internal Exception? Error;
        internal double Left=double.PositiveInfinity,Top=double.PositiveInfinity,Right=double.NegativeInfinity,Bottom=double.NegativeInfinity;
    }
    private static RunState GetRunState(RunSink* sink)=>(RunState)GCHandle.FromIntPtr(sink->Handle).Target!;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int RunQuery(RunSink* sink,Guid* iid,nint* value)
    {
        if(iid==null||value==null)return unchecked((int)0x80004003);
        if(*iid!=new Guid("00000000-0000-0000-c000-000000000046")&&*iid!=new Guid("ef8a8135-5cc6-45fe-8825-c5a0724eb819")&&*iid!=new Guid("eaf3a2da-ecf4-4d24-b644-b34f6842024b")){*value=0;return unchecked((int)0x80004002);}
        *value=(nint)sink;sink->References++;return 0;
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static uint RunAddRef(RunSink* sink)=>++sink->References;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static uint RunRelease(RunSink* sink)=>--sink->References;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int RunPixelSnapping(RunSink* sink,nint context,int* disabled){*disabled=1;return 0;}
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int RunTransform(RunSink* sink,nint context,Matrix* matrix){*matrix=new(0,0);return 0;}
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int RunPixelsPerDip(RunSink* sink,nint context,float* pixels){*pixels=1;return 0;}
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int RunGlyphs(RunSink* sink,nint context,float x,float y,uint measuringMode,GlyphRun* run,nint description,nint effect)
    {
        var state=GetRunState(sink);
        try
        {
            if(run==null||run->GlyphCount==0)return 0;
            var segments=NativeGlyphOutline.GetRunSegments(run->FontFace,run->EmSize,run->GlyphIndices,run->GlyphAdvances,run->GlyphOffsets,run->GlyphCount,(run->BidiLevel&1)!=0);
            double left=double.PositiveInfinity,top=double.PositiveInfinity,right=double.NegativeInfinity,bottom=double.NegativeInfinity;
            foreach(var segment in segments)
            {
                if(segment.Operation==MathPathOperation.Close)continue;
                Include(segment.X1,segment.Y1);if(segment.Operation==MathPathOperation.Cubic){Include(segment.X2,segment.Y2);Include(segment.X3,segment.Y3);}
            }
            if(double.IsPositiveInfinity(left))return 0;
            double by=y-state.Baseline;var bounds=new MathBounds(x+left,by+top,right-left,bottom-top);
            state.Paths.Add(new(segments,x,by,1,1,bounds));state.Left=Math.Min(state.Left,bounds.X);state.Top=Math.Min(state.Top,bounds.Y);state.Right=Math.Max(state.Right,bounds.X+bounds.Width);state.Bottom=Math.Max(state.Bottom,bounds.Y+bounds.Height);
            return 0;
            void Include(double px,double py){left=Math.Min(left,px);top=Math.Min(top,py);right=Math.Max(right,px);bottom=Math.Max(bottom,py);}
        }
        catch(Exception error){state.Error=error;return unchecked((int)0x80004005);}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int RunDecoration(RunSink* sink,nint context,float x,float y,nint decoration,nint effect)=>0;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int RunInline(RunSink* sink,nint context,float x,float y,nint inline,int sideways,int rtl,nint effect)=>unchecked((int)0x80004001);
}
