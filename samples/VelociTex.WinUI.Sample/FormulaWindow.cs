using System.Text.Json;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using VelociTex.Rendering.Direct2D;
using Windows.ApplicationModel.DataTransfer;

namespace VelociTex.WinUI.Sample;

internal sealed class FormulaWindow:Window
{
    private readonly StackPanel _root=new(){Padding=new Thickness(28),Spacing=18};
    private readonly List<MathView> _views=[];
    private readonly List<ScrollViewer> _formulaHosts=[];
    private readonly List<Border> _cards=[];
    private readonly List<(string Source,MathDocument? Document,MathDiagnostic Diagnostic)> _results=[];
    private readonly MathEngine _engine=new();
    private readonly MathWorkspace _workspace=new();
    private readonly string? _output;
    private readonly TextBlock _status=new(){FontSize=13,TextWrapping=TextWrapping.Wrap};
    private bool _dark;

    internal FormulaWindow(string[] args)
    {
        Title="VelociTex — 单公式原生排版";AppWindow.Resize(new Windows.Graphics.SizeInt32(1020,960));
        for(int i=0;i<args.Length-1;i++)if(args[i] is "--p1-smoke" or "--p2-smoke")_output=Path.GetFullPath(args[i+1]);
        _root.Children.Add(new TextBlock{Text="VelociTex",FontSize=30,FontWeight=Microsoft.UI.Text.FontWeights.SemiBold});
        _root.Children.Add(new TextBlock{Text="单次解析，共享几何，原生显示与 PNG / SVG / MathML · 聚焦公式后按 Ctrl+C 复制",FontSize=15,TextWrapping=TextWrapping.Wrap});
        Grid formulas=new(){ColumnSpacing=16,RowSpacing=16};
        formulas.ColumnDefinitions.Add(new(){Width=new GridLength(1,GridUnitType.Star)});formulas.ColumnDefinitions.Add(new(){Width=new GridLength(1,GridUnitType.Star)});
        formulas.RowDefinitions.Add(new(){Height=GridLength.Auto});formulas.RowDefinitions.Add(new(){Height=GridLength.Auto});
        string[] sources=[@"\int_0^1 x^2\,dx",@"\sum_{k=1}^{n}\frac{1}{k^2}",@"\binom{n}{k}+\hat{x}",@"A=\begin{pmatrix}1&2\\3&4\end{pmatrix}"];
        string[] names=["积分与微分变量","求和与分式","二项式与重音","矩阵环境"];
        for(int i=0;i<sources.Length;i++)
        {
            Border card=CreateFormulaCard(names[i],sources[i]);Grid.SetRow(card,i/2);Grid.SetColumn(card,i%2);formulas.Children.Add(card);
        }
        _root.Children.Add(formulas);
        _root.Children.Add(new TextBlock{Text="错误保留原始内容",FontSize=19,FontWeight=Microsoft.UI.Text.FontWeights.SemiBold});
        _root.Children.Add(CreateFormulaCard("不完整参数",@"\frac{a}{"));
        StackPanel actions=new(){Orientation=Orientation.Horizontal,Spacing=10};
        Button theme=new(){Content="切换明暗"};theme.Click+=(_,_)=>SetTheme(!_dark);actions.Children.Add(theme);
        Button copy=new(){Content="复制完整公式"};copy.Click+=(_,_)=>_views[1].CopySourceToClipboard();actions.Children.Add(copy);
        Button export=new(){Content="导出示例"};export.Click+=(_,_)=>{string target=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"VelociTex","exports");Export(target);_status.Text="已导出到 "+target;};actions.Children.Add(export);
        _root.Children.Add(actions);_root.Children.Add(_status);
        _status.Text="未实现的功能返回明确诊断。公式数据不依赖浏览器或 JavaScript。公式可通过键盘复制，并提供只读可访问性值。";
        Content=new ScrollViewer{Content=_root,VerticalScrollBarVisibility=ScrollBarVisibility.Auto,HorizontalScrollBarVisibility=ScrollBarVisibility.Disabled};
        SetTheme(false);_root.Loaded+=OnLoaded;Closed+=(_,_)=>{foreach(MathView view in _views)view.Dispose();};
    }
    private Border CreateFormulaCard(string title,string source)
    {
        StackPanel content=new(){Spacing=10};
        content.Children.Add(new TextBlock{Text=title,FontSize=15,FontWeight=Microsoft.UI.Text.FontWeights.SemiBold});
        content.Children.Add(new TextBlock{Text=source,FontSize=13,TextWrapping=TextWrapping.Wrap,IsTextSelectionEnabled=true});
        MathOptions options=new(){FontSize=30,DisplayMode=true};
        bool success=_engine.TryCompile(source,options,_workspace,default,out MathDocument? document,out MathDiagnostic diagnostic);
        _results.Add((source,document,diagnostic));
        if(success)
        {
            MathView view=new(){Result=document,AccessibleDescription=source};_views.Add(view);
            ScrollViewer formulaHost=new(){Content=view,HorizontalScrollBarVisibility=ScrollBarVisibility.Auto,VerticalScrollBarVisibility=ScrollBarVisibility.Disabled,MinHeight=72};
            _formulaHosts.Add(formulaHost);content.Children.Add(formulaHost);
            content.Children.Add(new TextBlock{Text=$"{document!.LogicalSize.Width:F2} × {document.LogicalSize.Height:F2} DIP · 基线 {document.Baseline:F2}",FontSize=12});
        }
        else content.Children.Add(new TextBlock{Text=$"{diagnostic.Code} [{diagnostic.Range.Start}, {diagnostic.Range.Length}] · {diagnostic.Message}",TextWrapping=TextWrapping.Wrap,FontSize=13,Foreground=new SolidColorBrush(ColorHelper.FromArgb(255,190,63,58))});
        Border card=new(){Child=content,Padding=new Thickness(16),CornerRadius=new CornerRadius(10),BorderThickness=new Thickness(1)};_cards.Add(card);return card;
    }
    private void SetTheme(bool dark)
    {
        _dark=dark;_root.RequestedTheme=dark?ElementTheme.Dark:ElementTheme.Light;
        _root.Background=new SolidColorBrush(dark?ColorHelper.FromArgb(255,23,26,32):ColorHelper.FromArgb(255,246,247,250));
        foreach(Border card in _cards){card.Background=new SolidColorBrush(dark?ColorHelper.FromArgb(255,31,35,43):Colors.White);card.BorderBrush=new SolidColorBrush(dark?ColorHelper.FromArgb(255,58,64,76):ColorHelper.FromArgb(255,220,224,232));}
        foreach(MathView view in _views)view.UseThemeColor();
    }
    private void Export(string directory)
    {
        Directory.CreateDirectory(directory);using WindowsMathRenderer renderer=new();
        for(int i=0;i<_results.Count;i++)
        {
            var item=_results[i];File.WriteAllText(Path.Combine(directory,$"formula-{i}.tex"),item.Source);
            if(item.Document is null)continue;
            renderer.WritePng(item.Document,Path.Combine(directory,$"formula-{i}.png"));
            renderer.WriteSvg(item.Document,Path.Combine(directory,$"formula-{i}.svg"));
            File.WriteAllText(Path.Combine(directory,$"formula-{i}.mathml"),MathMlWriter.Write(item.Document));
        }
    }
    private async void OnLoaded(object sender,RoutedEventArgs args)
    {
        if(_output is null)return;
        try
        {
            Directory.CreateDirectory(_output);File.Delete(Path.Combine(_output,"smoke-error.txt"));
            if(_views.Count!=4)throw new InvalidOperationException("A P1 sample formula did not compile successfully.");
            await Task.Delay(350);
            nint hwnd=WinRT.Interop.WindowNative.GetWindowHandle(this);
            await ControlAcceptance.RunAsync(this,_views[1]);
            foreach(bool dark in new[]{false,true})
            {
                SetTheme(dark);
                foreach(double scale in new[]{1d,1.25,1.5,2d})
                {
                    foreach(MathView view in _views)view.Rebuild(scale);_root.UpdateLayout();await Task.Delay(160);
                    if(_views.Any(view=>view.FormulaColor!=(dark?MathDrawingColor.White:MathDrawingColor.Black)))throw new InvalidOperationException("Formula color did not track the actual theme.");
                    await WindowCapture.CaptureAsync(hwnd,Path.Combine(_output,$"{(dark?"dark":"light")}-{scale*100:0}.png"));
                }
            }
            AppWindow.Resize(new Windows.Graphics.SizeInt32(640,960));_root.UpdateLayout();await Task.Delay(180);
            await WindowCapture.CaptureAsync(hwnd,Path.Combine(_output,"narrow-dark.png"));
            int unloaded=0,loaded=0;
            foreach(MathView view in _views){view.Unloaded+=(_,_)=>unloaded++;view.Loaded+=(_,_)=>loaded++;}
            foreach(ScrollViewer host in _formulaHosts)host.Content=null;
            _root.UpdateLayout();await Task.Delay(100);
            if(unloaded!=_views.Count||_views.Any(view=>view.IsLoaded))throw new InvalidOperationException("Every formula must actually unload and release its native display resources.");
            for(int i=0;i<_views.Count;i++)_formulaHosts[i].Content=_views[i];
            _root.UpdateLayout();await Task.Delay(180);await WindowCapture.CaptureAsync(hwnd,Path.Combine(_output,"reloaded.png"));
            if(loaded!=_views.Count||_views.Any(view=>!view.IsLoaded))throw new InvalidOperationException("Every formula must actually reload and recreate its native display resources.");
            Export(Path.Combine(_output,"exports"));
            using FileStream file=File.Create(Path.Combine(_output,"winui-report.json"));using Utf8JsonWriter json=new(file,new JsonWriterOptions{Indented=true});
            json.WriteStartObject();json.WriteString("scope","Live Core MathDocument native display, exports, keyboard copy and read-only UIA peer");json.WriteBoolean("keyboardCompleteSourceCopy",true);json.WriteBoolean("precompiledResultBindingAndClear",true);json.WriteBoolean("liveAutomationPeerReadOnlyValue",true);json.WriteNumber("actualUnloadedEvents",unloaded);json.WriteNumber("actualReloadedEvents",loaded);json.WriteBoolean("screenReaderAudioTested",false);json.WriteBoolean("nativeAot",!System.Runtime.CompilerServices.RuntimeFeature.IsDynamicCodeSupported);json.WriteNumber("processId",Environment.ProcessId);json.WriteString("capturedAt",DateTimeOffset.Now);json.WriteString("executableSha256",Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(Environment.ProcessPath!))));json.WriteNumber("actualMonitorRasterScale",_root.XamlRoot.RasterizationScale);
            json.WriteString("dpiBoundary","100/125/150/200% Composition surface density, monitor DPI unchanged");json.WriteStartArray("formulas");
            foreach(var item in _results){json.WriteStartObject();json.WriteString("source",item.Source);json.WriteBoolean("success",item.Document is not null);json.WriteString("diagnostic",item.Diagnostic.Code.ToString());if(item.Document is not null){json.WriteNumber("width",item.Document.LogicalSize.Width);json.WriteNumber("height",item.Document.LogicalSize.Height);json.WriteNumber("baseline",item.Document.Baseline);json.WriteNumber("glyphs",item.Document.Glyphs.Length);json.WriteNumber("rules",item.Document.Rules.Length);json.WriteNumber("paths",item.Document.Paths.Length);}json.WriteEndObject();}
            json.WriteEndArray();json.WriteEndObject();
        }
        catch(Exception error){Directory.CreateDirectory(_output);File.WriteAllText(Path.Combine(_output,"smoke-error.txt"),error.ToString());}
        finally{Close();}
    }
}
