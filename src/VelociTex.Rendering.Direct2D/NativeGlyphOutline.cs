using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using VelociTex;
using static VelociTex.Rendering.Direct2D.NativeMethods;

namespace VelociTex.Rendering.Direct2D;

internal static unsafe class NativeGlyphOutline
{
    internal static string GetPath(nint face,ushort glyph,float size)
    {
        return Build(face,glyph,size,false).Path!.ToString();
    }
    internal static MathPathSegment[] GetSegments(nint face,ushort glyph,float size) => Build(face,glyph,size,true).Segments!.ToArray();
    private static State Build(nint face,ushort glyph,float size,bool segments)
    {
        State state=new(segments);GCHandle handle=GCHandle.Alloc(state);
        nint* vtable=stackalloc nint[10];
        vtable[0]=(nint)(delegate* unmanaged[Stdcall]<Sink*,Guid*,nint*,int>)&Query;
        vtable[1]=(nint)(delegate* unmanaged[Stdcall]<Sink*,uint>)&AddRef;
        vtable[2]=(nint)(delegate* unmanaged[Stdcall]<Sink*,uint>)&ReleaseCallback;
        vtable[3]=(nint)(delegate* unmanaged[Stdcall]<Sink*,uint,void>)&SetMode;
        vtable[4]=vtable[3];
        vtable[5]=(nint)(delegate* unmanaged[Stdcall]<Sink*,Point,uint,void>)&Begin;
        vtable[6]=(nint)(delegate* unmanaged[Stdcall]<Sink*,Point*,uint,void>)&Lines;
        vtable[7]=(nint)(delegate* unmanaged[Stdcall]<Sink*,Bezier*,uint,void>)&Beziers;
        vtable[8]=(nint)(delegate* unmanaged[Stdcall]<Sink*,uint,void>)&End;
        vtable[9]=(nint)(delegate* unmanaged[Stdcall]<Sink*,int>)&Close;
        Sink sink=new(){Vtable=vtable,Handle=GCHandle.ToIntPtr(handle),References=1};
        try
        {
            float advance=0;
            Check(((delegate* unmanaged[Stdcall]<nint,float,ushort*,float*,nint,uint,int,int,Sink*,int>)Slot(face,14))(face,size,&glyph,&advance,0,1,0,0,&sink));
            if(state.Error is not null)throw new InvalidOperationException("Native glyph outline callback failed.",state.Error);
            return state;
        }
        finally{handle.Free();}
    }
    private static string F(float value)=>value.ToString("0.########",CultureInfo.InvariantCulture);
    [StructLayout(LayoutKind.Sequential)]private struct Sink{internal nint* Vtable;internal nint Handle;internal uint References;}
    private sealed class State(bool segments){internal readonly StringBuilder? Path=segments?null:new();internal readonly List<MathPathSegment>? Segments=segments?new():null;internal Exception? Error;}
    private static State GetState(Sink* sink)=>(State)GCHandle.FromIntPtr(sink->Handle).Target!;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static int Query(Sink* sink,Guid* iid,nint* result)
    {
        if(iid==null||result==null)return unchecked((int)0x80004003);
        if(*iid!=new Guid("00000000-0000-0000-c000-000000000046")&&*iid!=new Guid("2cd9069e-12e2-11dc-9fed-001143a055f9")){*result=0;return unchecked((int)0x80004002);}
        *result=(nint)sink;sink->References++;return 0;
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static uint AddRef(Sink* sink)=>++sink->References;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static uint ReleaseCallback(Sink* sink)=>--sink->References;
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static void SetMode(Sink* sink,uint mode){}
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static void Begin(Sink* sink,Point start,uint begin)
    {
        State state=GetState(sink);try{if(state.Path is {} path)path.Append('M').Append(F(start.X)).Append(' ').Append(F(start.Y));else state.Segments!.Add(new(MathPathOperation.Move,start.X,start.Y));}catch(Exception error){state.Error=error;}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static void Lines(Sink* sink,Point* points,uint count)
    {
        State state=GetState(sink);try{for(uint i=0;i<count;i++){if(state.Path is {} path)path.Append('L').Append(F(points[i].X)).Append(' ').Append(F(points[i].Y));else state.Segments!.Add(new(MathPathOperation.Line,points[i].X,points[i].Y));}}catch(Exception error){state.Error=error;}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static void Beziers(Sink* sink,Bezier* points,uint count)
    {
        State state=GetState(sink);try{for(uint i=0;i<count;i++){if(state.Path is {} path)path.Append('C').Append(F(points[i].P1.X)).Append(' ').Append(F(points[i].P1.Y)).Append(' ').Append(F(points[i].P2.X)).Append(' ').Append(F(points[i].P2.Y)).Append(' ').Append(F(points[i].P3.X)).Append(' ').Append(F(points[i].P3.Y));else state.Segments!.Add(new(MathPathOperation.Cubic,points[i].P1.X,points[i].P1.Y,points[i].P2.X,points[i].P2.Y,points[i].P3.X,points[i].P3.Y));}}catch(Exception error){state.Error=error;}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]
    private static void End(Sink* sink,uint end)
    {
        State state=GetState(sink);try{if(end==1){if(state.Path is {} path)path.Append('Z');else state.Segments!.Add(new(MathPathOperation.Close,0,0));}}catch(Exception error){state.Error=error;}
    }
    [UnmanagedCallersOnly(CallConvs=[typeof(CallConvStdcall)])]private static int Close(Sink* sink)=>GetState(sink).Error is null?0:unchecked((int)0x80004005);
}
