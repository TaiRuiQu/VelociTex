using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace VelociTex.WinUI.Bench;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            WinRT.ComWrappersSupport.InitializeComWrappers();
            Application.Start(_ =>
            {
                SynchronizationContext.SetSynchronizationContext(new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread()));
                BenchApp app = new(args);
                GC.KeepAlive(app);
            });
            return 0;
        }
        catch (Exception error)
        {
            File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "startup-error.txt"), error.ToString());
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}

public sealed partial class BenchApp : Application
{
    private readonly string[] _args;
    private Window? _window;

    public BenchApp(string[] args)
    {
        _args = args;
        UnhandledException += (_, error) => File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "unhandled-error.txt"), error.Exception.ToString());
        InitializeComponent();
    }

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        _window = new BenchWindow(_args);
        _window.Activate();
    }
}
