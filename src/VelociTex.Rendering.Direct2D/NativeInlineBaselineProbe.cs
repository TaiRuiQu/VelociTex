using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using static VelociTex.Rendering.Direct2D.NativeMethods;

namespace VelociTex.Rendering.Direct2D;

/// <summary>Exercises real DirectWrite inline-object layout and static unmanaged callbacks. Hard-coded metrics only.</summary>
public static unsafe class NativeInlineBaselineProbe
{
    /// <summary>Records actual wide and narrow line, glyph, and inline-object baseline positions.</summary>
    public static void WriteReport(string path)
        => Run(path,0,null,ProbeTheme.Light,null);

    /// <summary>Measures the hard-coded paragraph using the same real DirectWrite inline metrics as DrawParagraph.</summary>
    public static InlineParagraphMetrics MeasureParagraph(float width)
    {
        if(!float.IsFinite(width)||width<150||width>4096)throw new ArgumentOutOfRangeException(nameof(width));
        return Run(null,0,width,ProbeTheme.Light,null);
    }

    /// <summary>Draws body glyphs and explicit fraction/matrix inline probes via real DirectWrite callbacks. Caller owns BeginDraw/EndDraw.</summary>
    public static InlineParagraphMetrics DrawParagraph(nint renderTarget,float width,ProbeTheme theme=ProbeTheme.Light,string? fontDirectory=null)
    {
        if(renderTarget==0||!float.IsFinite(width)||width<150||width>4096)throw new ArgumentOutOfRangeException(nameof(width));
        return Run(null,renderTarget,width,theme,fontDirectory);
    }

    private static InlineParagraphMetrics Run(string? path,nint renderTarget,float? requestedWidth,ProbeTheme theme,string? fontDirectory)
    {
        int hr=CoInitializeEx(0,0);
        if(hr<0&&hr!=unchecked((int)0x80010106))Check(hr);
        nint factory=0,format=0,brush=0;
        uint originalTextAntialias=renderTarget!=0?((delegate* unmanaged[Stdcall]<nint,uint>)Slot(renderTarget,35))(renderTarget):0;
        NativeProbeRenderer? drawing=null;
        GCHandle handle=default;
        try
        {
            Guid iid=new("b859ee5a-d838-4b5b-a2e8-1adc7d93db48");
            Check(DWriteCreateFactory(1,&iid,&factory));
            fixed(char* family="Segoe UI")
            fixed(char* locale="en-US")
                Check(((delegate* unmanaged[Stdcall]<nint,char*,nint,uint,uint,uint,float,char*,nint*,int>)Slot(factory,15))(factory,family,0,400,0,5,18,locale,&format));
            ProbeState state=new();handle=GCHandle.Alloc(state);
            if(renderTarget!=0)
            {
                drawing=new(fontDirectory??Path.Combine(AppContext.BaseDirectory,"Fonts"));
                Color color=theme==ProbeTheme.Dark?new(.94f,.95f,.98f):new(.08f,.10f,.14f);
                Check(((delegate* unmanaged[Stdcall]<nint,Color*,nint,nint*,int>)Slot(renderTarget,8))(renderTarget,&color,0,&brush));
                ((delegate* unmanaged[Stdcall]<nint,uint,void>)Slot(renderTarget,34))(renderTarget,2);
                state.Target=renderTarget;state.Brush=brush;state.Drawing=drawing;
            }
            nint* inlineVtable=stackalloc nint[7];
            inlineVtable[0]=(nint)(delegate* unmanaged[Stdcall]<Callback*,Guid*,nint*,int>)&Query;
            inlineVtable[1]=(nint)(delegate* unmanaged[Stdcall]<Callback*,uint>)&AddRef;
            inlineVtable[2]=(nint)(delegate* unmanaged[Stdcall]<Callback*,uint>)&ReleaseCallback;
            inlineVtable[3]=(nint)(delegate* unmanaged[Stdcall]<Callback*,nint,nint,float,float,int,int,nint,int>)&InlineDraw;
            inlineVtable[4]=(nint)(delegate* unmanaged[Stdcall]<Callback*,InlineMetrics*,int>)&GetMetrics;
            inlineVtable[5]=(nint)(delegate* unmanaged[Stdcall]<Callback*,Overhang*,int>)&GetOverhang;
            inlineVtable[6]=(nint)(delegate* unmanaged[Stdcall]<Callback*,uint*,uint*,int>)&GetBreak;
            nint* rendererVtable=stackalloc nint[10];
            rendererVtable[0]=inlineVtable[0];rendererVtable[1]=inlineVtable[1];rendererVtable[2]=inlineVtable[2];
            rendererVtable[3]=(nint)(delegate* unmanaged[Stdcall]<Callback*,nint,int*,int>)&PixelSnapping;
            rendererVtable[4]=(nint)(delegate* unmanaged[Stdcall]<Callback*,nint,Matrix*,int>)&CurrentTransform;
            rendererVtable[5]=(nint)(delegate* unmanaged[Stdcall]<Callback*,nint,float*,int>)&PixelsPerDip;
            rendererVtable[6]=(nint)(delegate* unmanaged[Stdcall]<Callback*,nint,float,float,uint,nint,nint,nint,int>)&GlyphDraw;
            rendererVtable[7]=(nint)(delegate* unmanaged[Stdcall]<Callback*,nint,float,float,nint,nint,int>)&Decoration;
            rendererVtable[8]=rendererVtable[7];
            rendererVtable[9]=(nint)(delegate* unmanaged[Stdcall]<Callback*,nint,float,float,nint,int,int,nint,int>)&RendererInlineDraw;
            Callback fraction=new(){Vtable=inlineVtable,State=GCHandle.ToIntPtr(handle),References=1,Kind=1,Metrics=new(){Width=110,Height=60,Baseline=42}};
            Callback matrix=new(){Vtable=inlineVtable,State=GCHandle.ToIntPtr(handle),References=1,Kind=2,Metrics=new(){Width=110,Height=74,Baseline=43}};
            Callback renderer=new(){Vtable=rendererVtable,State=GCHandle.ToIntPtr(handle),References=1,Kind=3};
            const string text="Body text \uFFFC, a fraction; and \uFFFC, a matrix. End.";
            if(path is not null)Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
            using Stream stream=path is null?new MemoryStream():File.Create(path);
            using Utf8JsonWriter json=new(stream,new JsonWriterOptions{Indented=true});
            json.WriteStartObject();json.WriteString("kind","P0 actual DirectWrite inline callback probe with hard-coded formula metrics");
            json.WriteStartArray("cases");
            LineMetrics* lines=stackalloc LineMetrics[16];
            InlineParagraphMetrics result=default;
            foreach(float width in requestedWidth is float selected?new[]{selected}:new[]{780f,280f})
            {
                nint layout=0;
                try
                {
                    state.GlyphBaselines.Clear();state.Objects.Clear();state.Error=null;
                    fixed(char* value=text)Check(((delegate* unmanaged[Stdcall]<nint,char*,uint,nint,float,float,nint*,int>)Slot(factory,18))(factory,value,(uint)text.Length,format,width,400,&layout));
                    Check(((delegate* unmanaged[Stdcall]<nint,nint,TextRange,int>)Slot(layout,39))(layout,(nint)(&fraction),new((uint)text.IndexOf('\uFFFC'),1)));
                    Check(((delegate* unmanaged[Stdcall]<nint,nint,TextRange,int>)Slot(layout,39))(layout,(nint)(&matrix),new((uint)text.LastIndexOf('\uFFFC'),1)));
                    uint lineCount=0;
                    Check(((delegate* unmanaged[Stdcall]<nint,LineMetrics*,uint,uint*,int>)Slot(layout,59))(layout,lines,16,&lineCount));
                    Check(((delegate* unmanaged[Stdcall]<nint,nint,nint,float,float,int>)Slot(layout,58))(layout,0,(nint)(&renderer),0,0));
                    if(state.Error is not null)throw new InvalidOperationException("Inline callback failed.",state.Error);
                    if(state.Objects.Count!=2||state.GlyphBaselines.Count==0)throw new InvalidOperationException("DirectWrite did not invoke all expected callbacks.");
                    float top=0;
                    List<float> baselines=[];
                    for(uint line=0;line<lineCount;line++){baselines.Add(top+lines[line].Baseline);top+=lines[line].Height;}
                    result=new(width,top,lines[0].Baseline,(int)lineCount);
                    foreach(float y in state.GlyphBaselines)if(!baselines.Any(b=>Math.Abs(b-y)<.001f))throw new InvalidOperationException("Body glyph baseline differs from line metrics.");
                    foreach(var item in state.Objects)if(!baselines.Any(b=>Math.Abs(b-item.Top-item.Baseline)<.001f))throw new InvalidOperationException("Inline baseline differs from line metrics.");
                    json.WriteStartObject();json.WriteNumber("widthDip",width);json.WriteNumber("lineCount",lineCount);json.WriteBoolean("bodyAndInlineMatchLineBaselines",true);
                    json.WriteStartArray("lines");top=0;
                    for(uint line=0;line<lineCount;line++){json.WriteStartObject();json.WriteNumber("length",lines[line].Length);json.WriteNumber("top",top);json.WriteNumber("height",lines[line].Height);json.WriteNumber("baseline",lines[line].Baseline);json.WriteEndObject();top+=lines[line].Height;}json.WriteEndArray();
                    json.WriteStartArray("inlineObjects");foreach(var item in state.Objects){json.WriteStartObject();json.WriteString("kind",item.Kind==1?"fraction":"matrix");json.WriteNumber("x",item.X);json.WriteNumber("top",item.Top);json.WriteNumber("height",item.Height);json.WriteNumber("baseline",item.Baseline);json.WriteNumber("absoluteBaseline",item.Top+item.Baseline);json.WriteEndObject();}json.WriteEndArray();
                    json.WriteStartArray("bodyGlyphBaselines");foreach(float value in state.GlyphBaselines)json.WriteNumberValue(value);json.WriteEndArray();json.WriteEndObject();
                }
                finally{Release(ref layout);}
            }
            json.WriteEndArray();json.WriteNumber("fractionFinalRefCount",fraction.References);json.WriteNumber("matrixFinalRefCount",matrix.References);json.WriteEndObject();
            if(fraction.References!=1||matrix.References!=1)throw new InvalidOperationException("DirectWrite retained inline references after layout disposal.");
            return result;
        }
        finally{if(renderTarget!=0)((delegate* unmanaged[Stdcall]<nint,uint,void>)Slot(renderTarget,34))(renderTarget,originalTextAntialias);if(handle.IsAllocated)handle.Free();Release(ref brush);drawing?.Dispose();Release(ref format);Release(ref factory);if(hr>=0)CoUninitialize();}
    }

    [StructLayout(LayoutKind.Sequential)] private struct Callback{internal nint* Vtable;internal nint State;internal uint References,Kind;internal InlineMetrics Metrics;}
    [StructLayout(LayoutKind.Sequential)] private struct InlineMetrics{internal float Width,Height,Baseline;internal int SupportsSideways;}
    [StructLayout(LayoutKind.Sequential)] private struct Overhang{internal float Left,Top,Right,Bottom;}
    [StructLayout(LayoutKind.Sequential)] private struct TextRange(uint start,uint length){internal uint Start=start,Length=length;}
    [StructLayout(LayoutKind.Sequential)] private struct LineMetrics{internal uint Length,TrailingWhitespace,Newline;internal float Height,Baseline;internal int Trimmed;}
    private sealed class ProbeState{internal readonly List<float> GlyphBaselines=[];internal readonly List<(uint Kind,float X,float Top,float Height,float Baseline)> Objects=[];internal Exception? Error;internal nint Target,Brush;internal NativeProbeRenderer? Drawing;}
    private static ProbeState State(Callback* self)=>(ProbeState)GCHandle.FromIntPtr(self->State).Target!;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static int Query(Callback* self,Guid* iid,nint* result)
    {
        if(iid==null||result==null)return unchecked((int)0x80004003);
        bool supported=*iid==new Guid("00000000-0000-0000-c000-000000000046")||
            (self->Kind<3&&*iid==new Guid("8339fde3-106f-47ab-8373-1c6295eb10b3"))||
            (self->Kind==3&&(*iid==new Guid("ef8a8135-5cc6-45fe-8825-c5a0724eb819")||*iid==new Guid("eaf3a2da-ecf4-4d24-b644-b34f6842024b")));
        if(!supported){*result=0;return unchecked((int)0x80004002);}*result=(nint)self;self->References++;return 0;
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static uint AddRef(Callback* self)=>++self->References;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static uint ReleaseCallback(Callback* self)=>--self->References;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int GetMetrics(Callback* self,InlineMetrics* metrics){*metrics=self->Metrics;return 0;}
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int GetOverhang(Callback* self,Overhang* metrics){*metrics=default;return 0;}
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int GetBreak(Callback* self,uint* before,uint* after){*before=0;*after=0;return 0;}
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static int InlineDraw(Callback* self,nint context,nint renderer,float x,float y,int sideways,int rtl,nint effect)
    {
        ProbeState state=State(self);
        try{state.Objects.Add((self->Kind,x,y,self->Metrics.Height,self->Metrics.Baseline));if(state.Target!=0)state.Drawing!.DrawInlineProbe(state.Target,self->Kind,x,y,state.Brush);return 0;}catch(Exception error){state.Error=error;return unchecked((int)0x80004005);}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int PixelSnapping(Callback* self,nint context,int* disabled){*disabled=1;return 0;}
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int CurrentTransform(Callback* self,nint context,Matrix* matrix){*matrix=new(0,0);return 0;}
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int PixelsPerDip(Callback* self,nint context,float* value){*value=1;return 0;}
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static int GlyphDraw(Callback* self,nint context,float x,float y,uint mode,nint run,nint description,nint effect)
    {
        ProbeState state=State(self);try{state.GlyphBaselines.Add(y);if(state.Target!=0)((delegate* unmanaged[Stdcall]<nint,Point,nint,nint,uint,void>)Slot(state.Target,29))(state.Target,new(x,y),run,state.Brush,mode);return 0;}catch(Exception error){state.Error=error;return unchecked((int)0x80004005);}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int Decoration(Callback* self,nint context,float x,float y,nint decoration,nint effect)=>0;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static int RendererInlineDraw(Callback* self,nint context,float x,float y,nint inline,int sideways,int rtl,nint effect)=>
        ((delegate* unmanaged[Stdcall]<nint,nint,nint,float,float,int,int,nint,int>)Slot(inline,3))(inline,context,(nint)self,x,y,sideways,rtl,effect);
}

/// <summary>Measured logical paragraph bounds returned by the P0 DirectWrite host probe.</summary>
public readonly record struct InlineParagraphMetrics(float Width,float Height,float FirstBaseline,int LineCount);
