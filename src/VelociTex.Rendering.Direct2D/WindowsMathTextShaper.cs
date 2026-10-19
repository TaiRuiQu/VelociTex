using System.Runtime.InteropServices;
using VelociTex;
using static VelociTex.Rendering.Direct2D.NativeMethods;

namespace VelociTex.Rendering.Direct2D;

/// <summary>Creates system-font outlines for CJK text absent from KaTeX fonts, without retaining native faces.</summary>
/// <remarks>Calls are serialized and may come from any thread. Only independently owned scalar
/// outlines are cached, bounded by both count and estimated bytes. Each cold lookup owns its COM
/// initialization and releases its factory, collection, family, font and face before returning.
/// This implementation supports CJK scalar text, not complex script shaping. The selected system
/// font and glyph geometry depend on the Windows font installation and are reported in results.</remarks>
public sealed unsafe partial class WindowsMathTextShaper : IMathTextRunShaper
{
    private readonly object _gate = new();
    private readonly Dictionary<(int,MathFont),MathTextShape> _cache = new();
    private long _bytes;
    /// <summary>Gets the maximum number of retained scalar outlines.</summary>
    public int MaximumItems { get; }
    /// <summary>Gets the maximum estimated managed outline payload in bytes.</summary>
    public long MaximumBytes { get; }
    /// <summary>Gets the retained outline count.</summary>
    public int Count { get { lock(_gate)return _cache.Count; } }
    /// <summary>Gets the conservative managed cache payload estimate.</summary>
    public long EstimatedRetainedBytes { get { lock(_gate)return _bytes; } }
    /// <summary>Creates a bounded scalar outline cache. Zero budgets disable retention.</summary>
    public WindowsMathTextShaper(int maximumItems=512,long maximumBytes=4*1024*1024)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(maximumItems);ArgumentOutOfRangeException.ThrowIfNegative(maximumBytes);
        MaximumItems=maximumItems;MaximumBytes=maximumBytes;
    }
    /// <summary>Clears owned managed outlines. Existing documents keep their independent references.</summary>
    public void Clear(){lock(_gate){_cache.Clear();_bytes=0;}}
    /// <summary>Returns an actual CJK outline in em units, or false when no supported font has the scalar.</summary>
    public bool TryShape(int codePoint,MathFont font,out MathTextShape shape)
    {
        shape=default;
        if(!System.Text.Rune.IsValid(codePoint))return false;
        lock(_gate)
        {
            var key=(codePoint,font);if(_cache.TryGetValue(key,out shape))return true;
            if(!ShapeNative(codePoint,font,out shape))return false;
            long bytes=256+shape.Segments.Length*64L+shape.FontFamily.Length*2L;
            if(MaximumItems>0&&bytes<=MaximumBytes)
            {
                if(_cache.Count>=MaximumItems||_bytes>MaximumBytes-bytes){_cache.Clear();_bytes=0;}
                _cache.Add(key,shape);_bytes+=bytes;
            }
            return true;
        }
    }
    private static bool ShapeNative(int codePoint,MathFont requested,out MathTextShape shape)
    {
        shape=default;int hr=CoInitializeEx(0,0);if(hr<0&&hr!=unchecked((int)0x80010106))Check(hr);
        bool uninitialize=hr>=0;nint factory=0,collection=0,family=0,font=0,face=0;
        try
        {
            Guid iid=new("b859ee5a-d838-4b5b-a2e8-1adc7d93db48");Check(DWriteCreateFactory(1,&iid,&factory));
            Check(((delegate* unmanaged[Stdcall]<nint,nint*,int,int>)Slot(factory,3))(factory,&collection,0));
            bool cjk=codePoint is >=0x3000 and <=0x30FF or >=0x4E00 and <=0x9FAF or >=0xFF00 and <=0xFF60 or >=0xAC00 and <=0xD7AF;
            bool italic=requested is MathFont.MathItalic or MathFont.MainBoldItalic or MathFont.MathBoldItalic or MathFont.MathSansItalic or MathFont.MathSansBoldItalic;
            foreach(string familyName in cjk?new[]{"Noto Sans SC","NotoSansSC","Microsoft YaHei","Yu Gothic","Malgun Gothic"}:italic?new[]{"Times New Roman","Cambria Math","Segoe UI Symbol","Segoe UI","Microsoft YaHei"}:new[]{"Cambria Math","Times New Roman","Segoe UI Symbol","Segoe UI","Microsoft YaHei"})
            {
                uint index=0;int exists=0;
                fixed(char* name=familyName)Check(((delegate* unmanaged[Stdcall]<nint,char*,uint*,int*,int>)Slot(collection,5))(collection,name,&index,&exists));
                if(exists==0)continue;
                try
                {
                    Check(((delegate* unmanaged[Stdcall]<nint,uint,nint*,int>)Slot(collection,4))(collection,index,&family));
                    uint weight=requested is MathFont.MathBold or MathFont.MathBoldItalic or MathFont.MathSansBold?700u:400u;
                    uint style=requested is MathFont.MathItalic or MathFont.MainBoldItalic or MathFont.MathBoldItalic or MathFont.MathSansItalic or MathFont.MathSansBoldItalic?2u:0u;
                    Check(((delegate* unmanaged[Stdcall]<nint,uint,uint,uint,nint*,int>)Slot(family,7))(family,weight,5,style,&font));
                    Check(((delegate* unmanaged[Stdcall]<nint,nint*,int>)Slot(font,13))(font,&face));
                    uint scalar=(uint)codePoint;ushort glyph=0;
                    Check(((delegate* unmanaged[Stdcall]<nint,uint*,uint,ushort*,int>)Slot(face,11))(face,&scalar,1,&glyph));
                    if(glyph==0)continue;
                    // DWRITE_FONT_METRICS starts with designUnitsPerEm; reserve its complete struct.
                    ushort* metrics=stackalloc ushort[10];((delegate* unmanaged[Stdcall]<nint,ushort*,void>)Slot(face,8))(face,metrics);
                    GlyphMetrics glyphMetrics=default;Check(((delegate* unmanaged[Stdcall]<nint,ushort*,uint,GlyphMetrics*,int,int>)Slot(face,10))(face,&glyph,1,&glyphMetrics,0));
                    var segments=NativeGlyphOutline.GetSegments(face,glyph,1);
                    double left=double.PositiveInfinity,top=double.PositiveInfinity,right=double.NegativeInfinity,bottom=double.NegativeInfinity;
                    foreach(var segment in segments)
                    {
                        if(segment.Operation==MathPathOperation.Close)continue;
                        Point(segment.X1,segment.Y1);
                        if(segment.Operation==MathPathOperation.Cubic){Point(segment.X2,segment.Y2);Point(segment.X3,segment.Y3);}
                    }
                    var bounds=double.IsPositiveInfinity(left)?new MathBounds(0,0,0,0):new MathBounds(left,top,right-left,bottom-top);
                    shape=new(glyphMetrics.AdvanceWidth/(double)metrics[0],segments,bounds,familyName);return true;
                    void Point(double x,double y){left=Math.Min(left,x);top=Math.Min(top,y);right=Math.Max(right,x);bottom=Math.Max(bottom,y);}
                }
                finally{Release(ref face);Release(ref font);Release(ref family);}
            }
            return false;
        }
        finally{Release(ref face);Release(ref font);Release(ref family);Release(ref collection);Release(ref factory);if(uninitialize)CoUninitialize();}
    }
}
