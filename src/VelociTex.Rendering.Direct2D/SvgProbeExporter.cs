using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Xml;
using static VelociTex.Rendering.Direct2D.NativeMethods;

namespace VelociTex.Rendering.Direct2D;

internal sealed unsafe partial class NativeProbeRenderer
{
    /// <summary>Exports the same hard-coded geometry as self-contained SVG paths using private-font outlines.</summary>
    public void WriteSvg(string path, ProbeTheme theme = ProbeTheme.Light)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        using XmlWriter xml=XmlWriter.Create(path,new XmlWriterSettings {Indent=true,Encoding=new UTF8Encoding(false)});
        xml.WriteStartElement("svg","http://www.w3.org/2000/svg");
        xml.WriteAttributeString("width",F(Width));xml.WriteAttributeString("height",F(Height));
        xml.WriteAttributeString("viewBox",$"0 0 {F(Width)} {F(Height)}");
        xml.WriteAttributeString("role","img");
        xml.WriteElementString("title","P0 hard-coded native feasibility probe; not a formula engine");
        xml.WriteStartElement("defs");
        Dictionary<string,string> paths=new(StringComparer.Ordinal);
        foreach(Run run in _runs)
        {
            string key=$"{run.Italic}:{run.Size}:{run.Text}";
            if(paths.ContainsKey(key)) continue;
            string id=$"g{paths.Count}";paths.Add(key,id);
            xml.WriteStartElement("path");xml.WriteAttributeString("id",id);
            xml.WriteAttributeString("d",GetOutline(run));xml.WriteEndElement();
        }
        xml.WriteEndElement();
        xml.WriteStartElement("g");xml.WriteAttributeString("fill",theme==ProbeTheme.Dark?"#f0f2fa":"#141a24");
        foreach(Run run in _runs)
        {
            xml.WriteStartElement("use");
            xml.WriteAttributeString("href","#"+paths[$"{run.Italic}:{run.Size}:{run.Text}"]);
            xml.WriteAttributeString("transform",$"translate({F(run.X)} {F(run.Y)})");xml.WriteEndElement();
        }
        foreach(Rect rule in Rules)
        {
            xml.WriteStartElement("rect");xml.WriteAttributeString("x",F(rule.Left));xml.WriteAttributeString("y",F(rule.Top));
            xml.WriteAttributeString("width",F(rule.Right-rule.Left));xml.WriteAttributeString("height",F(rule.Bottom-rule.Top));xml.WriteEndElement();
        }
        StringBuilder radical=new();
        for(int i=0;i<Radical.Length;i++)radical.Append(i==0?'M':'L').Append(F(Radical[i].X)).Append(' ').Append(F(Radical[i].Y));
        radical.Append('Z');
        xml.WriteStartElement("path");xml.WriteAttributeString("d",radical.ToString());xml.WriteEndElement();
        xml.WriteEndElement();xml.WriteEndElement();
    }

    private static string F(float value)=>value.ToString("0.#####",CultureInfo.InvariantCulture);

    private string GetOutline(Run run)
    {
        OutlineState state=new();
        GCHandle handle=GCHandle.Alloc(state);
        nint* vtable=stackalloc nint[10];
        vtable[0]=(nint)(delegate* unmanaged[Stdcall]<OutlineSink*,Guid*,nint*,int>)&OutlineQuery;
        vtable[1]=(nint)(delegate* unmanaged[Stdcall]<OutlineSink*,uint>)&OutlineAddRef;
        vtable[2]=(nint)(delegate* unmanaged[Stdcall]<OutlineSink*,uint>)&OutlineRelease;
        vtable[3]=(nint)(delegate* unmanaged[Stdcall]<OutlineSink*,uint,void>)&OutlineSetMode;
        vtable[4]=(nint)(delegate* unmanaged[Stdcall]<OutlineSink*,uint,void>)&OutlineSetMode;
        vtable[5]=(nint)(delegate* unmanaged[Stdcall]<OutlineSink*,Point,uint,void>)&OutlineBegin;
        vtable[6]=(nint)(delegate* unmanaged[Stdcall]<OutlineSink*,Point*,uint,void>)&OutlineLines;
        vtable[7]=(nint)(delegate* unmanaged[Stdcall]<OutlineSink*,Bezier*,uint,void>)&OutlineBeziers;
        vtable[8]=(nint)(delegate* unmanaged[Stdcall]<OutlineSink*,uint,void>)&OutlineEnd;
        vtable[9]=(nint)(delegate* unmanaged[Stdcall]<OutlineSink*,int>)&OutlineClose;
        OutlineSink sink=new() {Vtable=vtable,Handle=GCHandle.ToIntPtr(handle),References=1};
        try
        {
            nint face=run.Italic?_italic:_main;
            fixed(ushort* glyphs=run.Glyphs)
            fixed(float* advances=run.Advances)
                Check(((delegate* unmanaged[Stdcall]<nint,float,ushort*,float*,nint,uint,int,int,OutlineSink*,int>)Slot(face,14))(face,run.Size,glyphs,advances,0,(uint)run.Glyphs.Length,0,0,&sink));
            if(state.Error is not null)throw new InvalidOperationException("Native outline callback failed.",state.Error);
            if(state.Path.Length==0)throw new InvalidOperationException("Native glyph outline is empty.");
            return state.Path.ToString();
        }
        finally{handle.Free();}
    }

    [StructLayout(LayoutKind.Sequential)] private struct OutlineSink {internal nint* Vtable;internal nint Handle;internal uint References;}
    private sealed class OutlineState {internal readonly StringBuilder Path=new();internal Exception? Error;}
    private static OutlineState State(OutlineSink* sink)=>(OutlineState)GCHandle.FromIntPtr(sink->Handle).Target!;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static int OutlineQuery(OutlineSink* sink,Guid* iid,nint* result)
    {
        if(iid==null||result==null)return unchecked((int)0x80004003);
        if(*iid!=new Guid("00000000-0000-0000-c000-000000000046")&&*iid!=new Guid("2cd9069e-12e2-11dc-9fed-001143a055f9")) {*result=0;return unchecked((int)0x80004002);}
        *result=(nint)sink;sink->References++;return 0;
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])] private static uint OutlineAddRef(OutlineSink* sink)=>++sink->References;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])] private static uint OutlineRelease(OutlineSink* sink)=>--sink->References;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])] private static void OutlineSetMode(OutlineSink* sink,uint mode) { }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static void OutlineBegin(OutlineSink* sink,Point start,uint begin)
    {
        OutlineState state=State(sink);
        try{state.Path.Append('M').Append(F(start.X)).Append(' ').Append(F(start.Y));}catch(Exception error){state.Error=error;}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static void OutlineLines(OutlineSink* sink,Point* points,uint count)
    {
        OutlineState state=State(sink);
        try{for(uint i=0;i<count;i++)state.Path.Append('L').Append(F(points[i].X)).Append(' ').Append(F(points[i].Y));}catch(Exception error){state.Error=error;}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static void OutlineBeziers(OutlineSink* sink,Bezier* points,uint count)
    {
        OutlineState state=State(sink);
        try{for(uint i=0;i<count;i++)state.Path.Append('C').Append(F(points[i].P1.X)).Append(' ').Append(F(points[i].P1.Y)).Append(' ').Append(F(points[i].P2.X)).Append(' ').Append(F(points[i].P2.Y)).Append(' ').Append(F(points[i].P3.X)).Append(' ').Append(F(points[i].P3.Y));}catch(Exception error){state.Error=error;}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static void OutlineEnd(OutlineSink* sink,uint end)
    {
        OutlineState state=State(sink);
        try{if(end==1)state.Path.Append('Z');}catch(Exception error){state.Error=error;}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])] private static int OutlineClose(OutlineSink* sink)=>State(sink).Error is null?0:unchecked((int)0x80004005);
}
