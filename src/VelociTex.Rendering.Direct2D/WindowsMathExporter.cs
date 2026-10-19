using System.Text;
using System.Xml;
using VelociTex;
using static VelociTex.Rendering.Direct2D.NativeMethods;

namespace VelociTex.Rendering.Direct2D;

public sealed unsafe partial class WindowsMathRenderer
{
    /// <summary>Writes a transparent PNG file, replacing existing contents and creating its parent directory.</summary>
    /// <param name="document">A complete compilation result.</param>
    /// <param name="path">Destination file path.</param>
    /// <param name="scale">Pixels per DIP, greater than zero and at most eight.</param>
    /// <param name="padding">Transparent margin in DIP, between zero and 1,024.</param>
    /// <param name="color">Optional foreground; the standard light-theme foreground is used by default.</param>
    public void WritePng(MathDocument document,string path,double scale=1,double padding=4,MathDrawingColor? color=null)
    {
        CheckThread();ArgumentNullException.ThrowIfNull(document);ArgumentException.ThrowIfNullOrWhiteSpace(path);
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        using FileStream output=File.Create(path);WritePng(document,output,scale,padding,color);
    }

    /// <summary>Writes transparent PNG bytes to any writable stream without closing it.</summary>
    /// <remarks>The native bitmap is limited to 8,192 pixels per side and 4,194,304 pixels total. The original source and layout remain unchanged.</remarks>
    /// <param name="document">A complete compilation result.</param>
    /// <param name="output">Caller-owned writable stream; seeking is not required.</param>
    /// <param name="scale">Pixels per DIP, greater than zero and at most eight.</param>
    /// <param name="padding">Transparent margin in DIP, between zero and 1,024.</param>
    /// <param name="color">Optional foreground; the standard light-theme foreground is used by default.</param>
    /// <exception cref="ArgumentOutOfRangeException">The scale, padding, color or output image dimensions exceed supported bounds.</exception>
    public void WritePng(MathDocument document,Stream output,double scale=1,double padding=4,MathDrawingColor? color=null)
    {
        CheckThread();ArgumentNullException.ThrowIfNull(document);ArgumentNullException.ThrowIfNull(output);
        if(!output.CanWrite)throw new ArgumentException("The output stream is not writable.",nameof(output));
        if(!double.IsFinite(scale)||scale<=0||scale>8)throw new ArgumentOutOfRangeException(nameof(scale));
        ExportBounds bounds=GetExportBounds(document,padding);
        uint width=(uint)Math.Max(1,Math.Ceiling(bounds.Width*scale)),height=(uint)Math.Max(1,Math.Ceiling(bounds.Height*scale));
        if(width>8192||height>8192||(ulong)width*height>4_194_304)throw new ArgumentOutOfRangeException(nameof(scale),"The export exceeds the 8192 pixel side or 4 megapixel limit.");
        MathDrawingColor foreground=color??MathDrawingColor.Black;foreground.Validate();
        nint wic=0,bitmap=0,target=0;
        Guid clsid=new("cacaf262-9370-4615-a13b-9f5539da4c0a"),iid=new("ec5ec8a9-c395-4314-9c77-54d7a935ff70"),pixel=new("6fddc324-4e03-4bfe-b185-3d77768dc910");
        using PreparedMath prepared=Prepare(document);
        try
        {
            Check(CoCreateInstance(&clsid,0,1,&iid,&wic));
            Check(((delegate* unmanaged[Stdcall]<nint,uint,uint,Guid*,uint,nint*,int>)Slot(wic,17))(wic,width,height,&pixel,1,&bitmap));
            TargetProperties properties=new(){Type=1,PixelFormat=new(87,1),DpiX=(float)(96*scale),DpiY=(float)(96*scale)};
            Check(((delegate* unmanaged[Stdcall]<nint,nint,TargetProperties*,nint*,int>)Slot(_factory,13))(_factory,bitmap,&properties,&target));
            BeginDraw(target);Clear(target);Draw(prepared,target,foreground,-bounds.X,-bounds.Y);EndDraw(target);
            EncodeDocumentPng(wic,bitmap,output,96*scale);
        }
        finally{Release(ref target);Release(ref bitmap);Release(ref wic);}
    }

    /// <summary>Writes a self-contained SVG file, replacing existing contents and creating its parent directory.</summary>
    /// <param name="document">A complete compilation result.</param>
    /// <param name="path">Destination file path.</param>
    /// <param name="padding">Transparent margin in DIP, between zero and 1,024.</param>
    /// <param name="color">Optional foreground; the standard light-theme foreground is used by default.</param>
    public void WriteSvg(MathDocument document,string path,double padding=4,MathDrawingColor? color=null)
    {
        CheckThread();ArgumentNullException.ThrowIfNull(document);ArgumentException.ThrowIfNullOrWhiteSpace(path);
        Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path))!);
        using FileStream output=File.Create(path);WriteSvg(document,output,padding,color);
    }

    /// <summary>Writes self-contained SVG paths to any writable stream without closing it.</summary>
    /// <remarks>Private glyph outlines and paths are embedded; no font installation or external resource is required to view the file.</remarks>
    /// <param name="document">A complete compilation result.</param>
    /// <param name="output">Caller-owned writable stream; seeking is not required.</param>
    /// <param name="padding">Transparent margin in DIP, between zero and 1,024.</param>
    /// <param name="color">Optional foreground; the standard light-theme foreground is used by default.</param>
    /// <exception cref="ArgumentOutOfRangeException">Padding or color exceeds supported bounds.</exception>
    /// <exception cref="InvalidOperationException">Document export geometry is invalid or exceeds the 32,768 DIP boundary.</exception>
    public void WriteSvg(MathDocument document,Stream output,double padding=4,MathDrawingColor? color=null)
    {
        CheckThread();ArgumentNullException.ThrowIfNull(document);ArgumentNullException.ThrowIfNull(output);
        if(!output.CanWrite)throw new ArgumentException("The output stream is not writable.",nameof(output));
        ExportBounds bounds=GetExportBounds(document,padding);MathDrawingColor foreground=color??MathDrawingColor.Black;foreground.Validate();
        using PreparedMath prepared=Prepare(document);
        using XmlWriter xml=XmlWriter.Create(output,new XmlWriterSettings{Indent=true,Encoding=new UTF8Encoding(false),CloseOutput=false});
        xml.WriteStartElement("svg","http://www.w3.org/2000/svg");
        xml.WriteAttributeString("width",Number(bounds.Width));xml.WriteAttributeString("height",Number(bounds.Height));
        xml.WriteAttributeString("viewBox",$"{Number(bounds.X)} {Number(bounds.Y)} {Number(bounds.Width)} {Number(bounds.Height)}");
        xml.WriteAttributeString("role","img");xml.WriteElementString("title",document.Source);
        xml.WriteStartElement("defs");
        Dictionary<(int Font,ushort Glyph,double Size),string> definitions=[];
        foreach(MathGlyph glyph in document.Glyphs.Span)
        {
            var key=(glyph.FontId,glyph.GlyphId,glyph.FontSize);if(definitions.ContainsKey(key))continue;
            string id="g"+definitions.Count;definitions.Add(key,id);
            if(!_outlines.TryGetValue(key,out string? outline))
            {
                outline=NativeGlyphOutline.GetPath(GetFace(glyph.FontId),glyph.GlyphId,(float)glyph.FontSize);
                long bytes=(long)outline.Length*sizeof(char)+64;
                if(_outlines.Count+_drawingOutlines.Count>=OutlineCacheLimit||_outlineBytes+_drawingOutlineBytes+bytes>OutlineCacheByteLimit)ClearOutlineCaches();
                if(bytes<=OutlineCacheByteLimit){_outlines[key]=outline;_outlineBytes+=bytes;}
            }
            xml.WriteStartElement("path");xml.WriteAttributeString("id",id);xml.WriteAttributeString("d",outline);xml.WriteEndElement();
        }
        for(int i=0;i<document.Paths.Length;i++)
        {
            MathBounds clip=document.Paths.Span[i].ClipBounds;
            xml.WriteStartElement("clipPath");xml.WriteAttributeString("id","c"+i);xml.WriteAttributeString("clipPathUnits","userSpaceOnUse");
            WriteRect(xml,clip.X,clip.Y,clip.Width,clip.Height);xml.WriteEndElement();
        }
        xml.WriteEndElement();
        xml.WriteStartElement("g");
        xml.WriteAttributeString("fill",$"#{(int)Math.Round(foreground.R*255):x2}{(int)Math.Round(foreground.G*255):x2}{(int)Math.Round(foreground.B*255):x2}");
        if(foreground.A!=1)xml.WriteAttributeString("fill-opacity",Number(foreground.A));
        if(document.PaintStates.Length!=0)
        {
            foreach(MathPaintCommand command in document.PaintCommands.Span)
            {
                MathPaintState state=document.PaintStates.Span[command.StateIndex];if(state.Color is {A:0})continue;
                bool wrapped=state.Color is not null||state.Transform!=MathTransform.Identity;
                if(wrapped)
                {
                    xml.WriteStartElement("g");
                    if(state.Color is {} ink){xml.WriteAttributeString("fill",$"#{ink.R:x2}{ink.G:x2}{ink.B:x2}");xml.WriteAttributeString("fill-opacity",Number(ink.A/255d));}
                    if(state.Transform!=MathTransform.Identity){var t=state.Transform;xml.WriteAttributeString("transform",$"matrix({Number(t.M11)} {Number(t.M12)} {Number(t.M21)} {Number(t.M22)} {Number(t.Dx)} {Number(t.Dy)})");}
                }
                switch(command.Kind)
                {
                    case MathPaintKind.Glyph:WriteGlyph(document.Glyphs.Span[command.Index]);break;
                    case MathPaintKind.Rule:var rule=document.Rules.Span[command.Index];WriteRect(xml,rule.X,rule.Y,rule.Width,rule.Height);break;
                    case MathPaintKind.Path:WritePath(command.Index);break;
                    default:throw new InvalidOperationException("Unknown retained painting command.");
                }
                if(wrapped)xml.WriteEndElement();
            }
        }
        else
        {
            foreach(MathGlyph glyph in document.Glyphs.Span)WriteGlyph(glyph);
            foreach(MathRule rule in document.Rules.Span)WriteRect(xml,rule.X,rule.Y,rule.Width,rule.Height);
            for(int i=0;i<document.Paths.Length;i++)WritePath(i);
        }
        xml.WriteEndElement();xml.WriteEndElement();
        void WriteGlyph(MathGlyph glyph)
        {
            xml.WriteStartElement("use");xml.WriteAttributeString("href","#"+definitions[(glyph.FontId,glyph.GlyphId,glyph.FontSize)]);
            xml.WriteAttributeString("transform",$"translate({Number(glyph.X)} {Number(glyph.Y)})");xml.WriteEndElement();
        }
        void WritePath(int i)
        {
            MathPath data=document.Paths.Span[i];
            xml.WriteStartElement("g");xml.WriteAttributeString("clip-path","url(#c"+i+")");
            xml.WriteStartElement("path");xml.WriteAttributeString("transform",$"translate({Number(data.X)} {Number(data.Y)}) scale({Number(data.ScaleX)} {Number(data.ScaleY)})");
            StringBuilder pathData=new();
            foreach(MathPathSegment segment in data.Segments.Span)
            {
                switch(segment.Operation)
                {
                    case MathPathOperation.Move:pathData.Append('M').Append(Number(segment.X1)).Append(' ').Append(Number(segment.Y1));break;
                    case MathPathOperation.Line:pathData.Append('L').Append(Number(segment.X1)).Append(' ').Append(Number(segment.Y1));break;
                    case MathPathOperation.Cubic:pathData.Append('C').Append(Number(segment.X1)).Append(' ').Append(Number(segment.Y1)).Append(' ').Append(Number(segment.X2)).Append(' ').Append(Number(segment.Y2)).Append(' ').Append(Number(segment.X3)).Append(' ').Append(Number(segment.Y3));break;
                    case MathPathOperation.Close:pathData.Append('Z');break;
                }
            }
            xml.WriteAttributeString("d",pathData.ToString());xml.WriteEndElement();xml.WriteEndElement();
        }
    }

    private static void WriteRect(XmlWriter xml,double x,double y,double width,double height)
    {
        xml.WriteStartElement("rect");xml.WriteAttributeString("x",Number(x));xml.WriteAttributeString("y",Number(y));xml.WriteAttributeString("width",Number(width));xml.WriteAttributeString("height",Number(height));xml.WriteEndElement();
    }
    private static ExportBounds GetExportBounds(MathDocument document,double padding)
    {
        if(!double.IsFinite(padding)||padding<0||padding>1024)throw new ArgumentOutOfRangeException(nameof(padding));
        MathBounds ink=document.InkBounds;
        double x=Math.Min(0,ink.X)-padding,y=Math.Min(0,ink.Y)-padding;
        double width=Math.Max(document.LogicalSize.Width,ink.X+ink.Width)-x+padding;
        double height=Math.Max(document.LogicalSize.Height,ink.Y+ink.Height)-y+padding;
        if(!double.IsFinite(width)||!double.IsFinite(height)||width>32768||height>32768||width<0||height<0)throw new InvalidOperationException("Document export bounds are invalid or too large.");
        return new(x,y,Math.Max(1,width),Math.Max(1,height));
    }
    private readonly record struct ExportBounds(double X,double Y,double Width,double Height);
    private static void EncodeDocumentPng(nint wic,nint bitmap,Stream output,double dpi)
    {
        nint stream=0,encoder=0,frame=0,options=0;
        try
        {
            Check(CreateStreamOnHGlobal(0,1,&stream));
            Guid format=new("1b7cfaf4-713f-473c-bbcd-6137425faeaf");
            Check(((delegate* unmanaged[Stdcall]<nint,Guid*,nint,nint*,int>)Slot(wic,8))(wic,&format,0,&encoder));
            Check(((delegate* unmanaged[Stdcall]<nint,nint,uint,int>)Slot(encoder,3))(encoder,stream,2));
            Check(((delegate* unmanaged[Stdcall]<nint,nint*,nint*,int>)Slot(encoder,10))(encoder,&frame,&options));
            Check(((delegate* unmanaged[Stdcall]<nint,nint,int>)Slot(frame,3))(frame,options));
            Check(((delegate* unmanaged[Stdcall]<nint,double,double,int>)Slot(frame,5))(frame,dpi,dpi));
            Check(((delegate* unmanaged[Stdcall]<nint,nint,nint,int>)Slot(frame,11))(frame,bitmap,0));
            Check(((delegate* unmanaged[Stdcall]<nint,int>)Slot(frame,12))(frame));Check(((delegate* unmanaged[Stdcall]<nint,int>)Slot(encoder,11))(encoder));
            Check(((delegate* unmanaged[Stdcall]<nint,long,uint,ulong*,int>)Slot(stream,5))(stream,0,0,null));
            byte[] buffer=new byte[64*1024];
            fixed(byte* bytes=buffer)
            {
                while(true)
                {
                    uint read=0;
                    Check(((delegate* unmanaged[Stdcall]<nint,byte*,uint,uint*,int>)Slot(stream,3))(stream,bytes,(uint)buffer.Length,&read));
                    if(read==0)break;output.Write(buffer,0,(int)read);
                }
            }
        }
        finally{Release(ref options);Release(ref frame);Release(ref encoder);Release(ref stream);}
    }
}
