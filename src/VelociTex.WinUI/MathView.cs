using System.Numerics;
using System.Runtime.InteropServices;
using Microsoft.Graphics.DirectX;
using Microsoft.UI.Composition;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Hosting;
using VelociTex.Rendering.Direct2D;

namespace VelociTex.WinUI;

/// <summary>Displays one independently owned Core result on a native Composition surface.</summary>
public sealed unsafe partial class MathView : Grid,IDisposable
{
    // Register as object because MathDocument is a plain managed immutable type, not a WinRT runtime class.
    public static readonly DependencyProperty ResultProperty=DependencyProperty.Register(nameof(Result),typeof(object),typeof(MathView),new PropertyMetadata(null,OnResultChanged));
    private WindowsMathRenderer? _renderer;
    private PreparedMath? _prepared;
    private CompositionGraphicsDevice? _graphics;
    private CompositionDrawingSurface? _surface;
    private CompositionSurfaceBrush? _brush;
    private SpriteVisual? _visual;
    private XamlRoot? _observedRoot;
    private nint _device;
    private double _scale=1;
    private MathDrawingColor _color=MathDrawingColor.Black;
    private bool _disposed;
    public MathDocument? Result{get=>(MathDocument?)GetValue(ResultProperty);set=>SetValue(ResultProperty,value);}
    public double BaselineDip=>Result is null?0:Result.Baseline+4-Math.Min(0,Result.InkBounds.Y);
    public MathDrawingColor FormulaColor{get=>_color;set{_color=value;if(IsLoaded)Redraw();}}
    public string? AccessibleDescription{get;set;}

    public MathView()
    {
        HorizontalAlignment=HorizontalAlignment.Left;
        Loaded+=OnLoaded;Unloaded+=OnUnloaded;
    }
    private static void OnResultChanged(DependencyObject sender,DependencyPropertyChangedEventArgs args)
    {
        MathView view=(MathView)sender;view.ResizeToResult();if(view.IsLoaded)view.Rebuild();
    }
    private void ResizeToResult()
    {
        MathDocument? document=Result;
        if(document is null){Width=Height=0;return;}
        Width=Math.Max(document.LogicalSize.Width,document.InkBounds.X+document.InkBounds.Width)-Math.Min(0,document.InkBounds.X)+8;
        Height=Math.Max(document.LogicalSize.Height,document.InkBounds.Y+document.InkBounds.Height)-Math.Min(0,document.InkBounds.Y)+8;
        AutomationProperties.SetName(this,AccessibleDescription??document.Source);
    }
    private void OnLoaded(object sender,RoutedEventArgs args)
    {
        _observedRoot=XamlRoot;if(_observedRoot is not null)_observedRoot.Changed+=OnRootChanged;
        ResizeToResult();Rebuild();
    }
    private void OnUnloaded(object sender,RoutedEventArgs args)
    {
        if(_observedRoot is not null)_observedRoot.Changed-=OnRootChanged;_observedRoot=null;Release();
    }
    private void OnRootChanged(XamlRoot sender,XamlRootChangedEventArgs args)
    {
        if(Math.Abs(sender.RasterizationScale-_scale)>.0001)Rebuild();
    }
    /// <summary>Recreates owned native resources. A scale override is used only by automated surface-density acceptance.</summary>
    public void Rebuild(double? scaleOverride=null)
    {
        ObjectDisposedException.ThrowIf(_disposed,this);Release();MathDocument? document=Result;if(document is null||!IsLoaded)return;
        _scale=scaleOverride??XamlRoot?.RasterizationScale??1;
        if(!double.IsFinite(_scale)||_scale<=0||_scale>8)throw new ArgumentOutOfRangeException(nameof(scaleOverride));
        double pixelWidth=Math.Ceiling(Width*_scale),pixelHeight=Math.Ceiling(Height*_scale);
        if(pixelWidth>8192||pixelHeight>8192||pixelWidth*pixelHeight>4_194_304)throw new InvalidOperationException("The native surface exceeds the 8192 pixel side or 4 megapixel limit.");
        try
        {
            _renderer=new WindowsMathRenderer();_prepared=_renderer.Prepare(document);
            Compositor compositor=ElementCompositionPreview.GetElementVisual(this).Compositor;
            _device=WindowsMathRenderer.CreateWarpD2DDevice();
            nint compositorPointer=WinRT.MarshalInspectable<Compositor>.FromManaged(compositor),interop=0,graphicsPointer=0;
            try
            {
                Guid iid=new("FAB19398-6D19-4D8A-B752-8F096C396069");Check(Marshal.QueryInterface(compositorPointer,in iid,out interop));
                Check(((delegate* unmanaged[Stdcall]<nint,nint,nint*,int>)(*(nint**)interop)[3])(interop,_device,&graphicsPointer));
                _graphics=WinRT.MarshalInspectable<CompositionGraphicsDevice>.FromAbi(graphicsPointer);
            }
            finally{Free(ref graphicsPointer);Free(ref interop);Free(ref compositorPointer);}
            _surface=_graphics.CreateDrawingSurface(new Windows.Foundation.Size(pixelWidth,pixelHeight),DirectXPixelFormat.B8G8R8A8UIntNormalized,DirectXAlphaMode.Premultiplied);
            _brush=compositor.CreateSurfaceBrush(_surface);_visual=compositor.CreateSpriteVisual();_visual.Size=new((float)Width,(float)Height);_visual.Brush=_brush;
            ElementCompositionPreview.SetElementChildVisual(this,_visual);Redraw();
        }
        catch{Release();throw;}
    }
    public void Redraw()
    {
        if(_surface is null||_renderer is null||_prepared is null)return;
        nint surfacePointer=WinRT.MarshalInspectable<CompositionDrawingSurface>.FromManaged(_surface),interop=0,context=0;bool begun=false;
        try
        {
            Guid iid=new("2D6355C2-AD57-4EAE-92E4-4C3EFF65D578");Check(Marshal.QueryInterface(surfacePointer,in iid,out interop));
            iid=new("E8F7FE7A-191C-466D-AD95-975678BDA998");Point offset;
            Check(((delegate* unmanaged[Stdcall]<nint,void*,Guid*,nint*,Point*,int>)(*(nint**)interop)[3])(interop,null,&iid,&context,&offset));begun=true;
            nint* table=*(nint**)context;
            ((delegate* unmanaged[Stdcall]<nint,float,float,void>)table[51])(context,(float)(96*_scale),(float)(96*_scale));
            Matrix transform=new(){M11=1,M22=1,Dx=(float)(offset.X/_scale),Dy=(float)(offset.Y/_scale)};
            ((delegate* unmanaged[Stdcall]<nint,Matrix*,void>)table[30])(context,&transform);
            ((delegate* unmanaged[Stdcall]<nint,nint,void>)table[47])(context,0);
            MathDocument document=_prepared.Document;
            _renderer.Draw(_prepared,context,_color,4-Math.Min(0,document.InkBounds.X),4-Math.Min(0,document.InkBounds.Y));
        }
        finally
        {
            Free(ref context);
            try{if(begun)Check(((delegate* unmanaged[Stdcall]<nint,int>)(*(nint**)interop)[4])(interop));}
            finally{Free(ref interop);Free(ref surfacePointer);}
        }
    }
    protected override AutomationPeer OnCreateAutomationPeer()=>new MathViewAutomationPeer(this);
    private void Release()
    {
        ElementCompositionPreview.SetElementChildVisual(this,null);
        _visual?.Dispose();_visual=null;_brush?.Dispose();_brush=null;_surface?.Dispose();_surface=null;_graphics?.Dispose();_graphics=null;
        _prepared?.Dispose();_prepared=null;_renderer?.Dispose();_renderer=null;Free(ref _device);
    }
    public void Dispose()
    {
        if(_disposed)return;if(_observedRoot is not null)_observedRoot.Changed-=OnRootChanged;_observedRoot=null;Release();_disposed=true;
    }
    private static void Check(int hr){if(hr<0)Marshal.ThrowExceptionForHR(hr);}
    private static void Free(ref nint pointer){if(pointer!=0){Marshal.Release(pointer);pointer=0;}}
    [StructLayout(LayoutKind.Sequential)]private struct Point{internal int X,Y;}
    [StructLayout(LayoutKind.Sequential)]private struct Matrix{internal float M11,M12,M21,M22,Dx,Dy;}
    private sealed partial class MathViewAutomationPeer(MathView owner):FrameworkElementAutomationPeer(owner)
    {
        protected override string GetClassNameCore()=>nameof(MathView);
        protected override AutomationControlType GetAutomationControlTypeCore()=>AutomationControlType.Text;
        protected override string GetNameCore()=>owner.AccessibleDescription??owner.Result?.Source??string.Empty;
    }
}
