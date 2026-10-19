using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace VelociTex.WinUI.Sample;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            Log("Process started");
            WinRT.ComWrappersSupport.InitializeComWrappers();
            Application.Start(parameters =>
            {
                Log("Application callback");
                SynchronizationContext.SetSynchronizationContext(new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread()));
                _ = new ProbeApp(args);
            });
            return 0;
        }
        catch (Exception error)
        {
            File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "startup-error.txt"), error.ToString());
            return 1;
        }
    }
    internal static void Log(string text) => File.AppendAllText(Path.Combine(AppContext.BaseDirectory, "probe.log"), DateTimeOffset.Now.ToString("O") + " " + text + Environment.NewLine);
}

public sealed partial class ProbeApp : Application
{
    private readonly string[] args;
    private Window? window;
    public ProbeApp(string[] args)
    {
        Program.Log("App constructor");
        this.args = args;
        UnhandledException += (_, error) => Program.Log("Unhandled: " + error.Exception);
        InitializeComponent();
        Program.Log("Resources ready");
    }
    protected override void OnLaunched(LaunchActivatedEventArgs launched)
    {
        Program.Log("Launching window");
        window = args.Contains("--perf") ? new PerfWindow(args) : args.Contains("--p0") ? new ProbeWindow(args) : new FormulaWindow(args);
        window.Activate();
        Program.Log("Window activated");
    }
}
