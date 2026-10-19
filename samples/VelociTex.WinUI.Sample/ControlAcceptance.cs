using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Data;
using Windows.ApplicationModel.DataTransfer;

namespace VelociTex.WinUI.Sample;

/// <summary>Exercises the live XAML peer, system clipboard and keyboard accelerator in the owned window.</summary>
internal static partial class ControlAcceptance
{
    internal static async Task RunAsync(Window window,MathView view)
    {
        MathDocument result=view.Result??throw new InvalidOperationException("A compiled test formula is required.");
        AutomationPeer peer=FrameworkElementAutomationPeer.CreatePeerForElement(view);
        if(peer.GetClassName()!=nameof(MathView)||peer.GetAutomationControlType()!=AutomationControlType.Text)
            throw new InvalidOperationException("The formula did not expose its text automation peer.");
        if(peer.GetPattern(PatternInterface.Value) is not IValueProvider value||!value.IsReadOnly||value.Value!=result.Source)
            throw new InvalidOperationException("UIA Value did not contain the complete read-only formula source.");
        string previous=view.AccessibleDescription;
        view.AccessibleDescription="Summation of reciprocals of squares, read-only mathematical expression";
        if(peer.GetName()!=view.AccessibleDescription||!peer.GetHelpText().Contains("Control+C",StringComparison.Ordinal))
            throw new InvalidOperationException("The formula description or copy instructions are missing from UIA.");
        view.AccessibleDescription=previous;
        view.Tag="A bound mathematical description";
        view.SetBinding(MathView.AccessibleDescriptionProperty,new Binding{Source=view,Path=new PropertyPath(nameof(view.Tag)),Mode=BindingMode.OneWay});
        if(peer.GetName()!="A bound mathematical description")throw new InvalidOperationException("The accessibility description did not support binding.");
        view.Tag="Updated bound mathematical description";
        await Task.Yield();
        if(peer.GetName()!="Updated bound mathematical description")throw new InvalidOperationException("The accessibility description binding did not update UIA.");
        view.ClearValue(MathView.AccessibleDescriptionProperty);view.AccessibleDescription=previous;
        AutomationProperties.SetName(view,"Host supplied formula name");
        if(peer.GetName()!="Host supplied formula name")throw new InvalidOperationException("The explicit host automation name did not take precedence.");
        view.ClearValue(AutomationProperties.NameProperty);
        bool rejected=false;
        try{value.SetValue("modified");}catch(InvalidOperationException){rejected=true;}
        if(!rejected||view.Result.Source!=result.Source)throw new InvalidOperationException("The UIA formula must remain read-only.");

        // Bind through an existing WinRT dependency property: no reflection-only CLR
        // binding metadata or source generator exemptions are needed under AOT.
        view.Tag=result;
        view.SetBinding(MathView.ResultProperty,new Binding{Source=view,Path=new PropertyPath(nameof(view.Tag)),Mode=BindingMode.OneWay});
        view.Tag=null;
        await Task.Yield();
        if(view.Result is not null||value.Value.Length!=0)throw new InvalidOperationException("The bound null result did not clear display and UIA value.");
        view.Tag=result;
        await Task.Yield();
        if(!ReferenceEquals(view.Result,result)||value.Value!=result.Source)throw new InvalidOperationException("The bound result did not restore display and UIA value.");

        // Preserve the desktop clipboard while verifying the actual OS copy route.
        DataPackage restoration=new();
        DataPackageView existing=Clipboard.GetContent();
        foreach(string format in existing.AvailableFormats)restoration.SetData(format,await existing.GetDataAsync(format));
        try
        {
            DataPackage sentinel=new();sentinel.SetText("VelociTex keyboard acceptance sentinel");Clipboard.SetContent(sentinel);
            nint hwnd=WinRT.Interop.WindowNative.GetWindowHandle(window);
            window.AppWindow.Show();window.Activate();ShowWindow(hwnd,1);SetForegroundWindow(hwnd);
            await Task.Delay(120);
            if(GetForegroundWindow()!=hwnd)
            {
                uint foregroundThread=GetWindowThreadProcessId(GetForegroundWindow(),0),currentThread=GetCurrentThreadId();
                bool attached=foregroundThread!=0&&foregroundThread!=currentThread&&AttachThreadInput(currentThread,foregroundThread,true);
                try{SetForegroundWindow(hwnd);}
                finally{if(attached)AttachThreadInput(currentThread,foregroundThread,false);}
                await Task.Delay(120);
            }
            if(GetForegroundWindow()!=hwnd)throw new InvalidOperationException($"The owned acceptance window {hwnd} could not receive foreground input (foreground {GetForegroundWindow()}).");
            if(!view.Focus(FocusState.Keyboard))throw new InvalidOperationException("The formula could not receive keyboard focus.");
            SendCopy();
            for(int attempt=0;attempt<20;attempt++)
            {
                await Task.Delay(50);
                DataPackageView copied=Clipboard.GetContent();
                if(copied.Contains(StandardDataFormats.Text)&&await copied.GetTextAsync()==result.Source)return;
            }
            throw new InvalidOperationException("Control+C did not copy the complete original TeX source.");
        }
        finally{Clipboard.SetContent(restoration);Clipboard.Flush();}
    }

    private static unsafe void SendCopy()
    {
        Input* input=stackalloc Input[4];
        input[0]=new(){Type=1,Keyboard=new(){VirtualKey=0x11}};
        input[1]=new(){Type=1,Keyboard=new(){VirtualKey=0x43}};
        input[2]=new(){Type=1,Keyboard=new(){VirtualKey=0x43,Flags=2}};
        input[3]=new(){Type=1,Keyboard=new(){VirtualKey=0x11,Flags=2}};
        if(SendInput(4,input,sizeof(Input))!=4)throw new InvalidOperationException("The owned window did not accept the copy keystroke.");
    }
    [StructLayout(LayoutKind.Explicit,Size=40)]private struct Input{[FieldOffset(0)]internal uint Type;[FieldOffset(8)]internal KeyboardInput Keyboard;}
    [StructLayout(LayoutKind.Sequential)]private struct KeyboardInput{internal ushort VirtualKey,Scan;internal uint Flags,Time;internal nuint ExtraInfo;}
    [LibraryImport("user32.dll",SetLastError=true)][return:MarshalAs(UnmanagedType.Bool)]private static partial bool SetForegroundWindow(nint hwnd);
    [LibraryImport("user32.dll")][return:MarshalAs(UnmanagedType.Bool)]private static partial bool ShowWindow(nint hwnd,int command);
    [LibraryImport("user32.dll")]private static partial nint GetForegroundWindow();
    [LibraryImport("user32.dll")]private static partial uint GetWindowThreadProcessId(nint hwnd,nint processId);
    [LibraryImport("kernel32.dll")]private static partial uint GetCurrentThreadId();
    [LibraryImport("user32.dll")][return:MarshalAs(UnmanagedType.Bool)]private static partial bool AttachThreadInput(uint source,uint destination,[MarshalAs(UnmanagedType.Bool)]bool attach);
    [LibraryImport("user32.dll",SetLastError=true)]private static unsafe partial uint SendInput(uint count,Input* input,int size);
}
