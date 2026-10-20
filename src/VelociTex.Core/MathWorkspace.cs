namespace VelociTex;

/// <summary>Call-exclusive reusable native scratch buffers.</summary>
public sealed class MathWorkspace : IDisposable
{
    private int _busy;
    private NativeWorkspaceHandle? _native;
    internal NativeWorkspaceHandle NativeHandle=>_native??=new();
    internal readonly List<SemanticNode> Nodes=new(256);
    // Retained internal compatibility counters; token storage belongs to native code.
    internal readonly List<byte> Pending=new(0),Expanded=new(0);
    internal readonly List<MathWarning> Warnings=new(8);
    internal NativeResultHandle? ParserResult;
    internal NativeInterop.View ParserView;
    internal bool Enter()=>Interlocked.CompareExchange(ref _busy,1,0)==0;
    internal void Leave()
    {
        ParserResult?.Dispose();ParserResult=null;Nodes.Clear();Warnings.Clear();
        if(Nodes.Capacity>4096)Nodes.Capacity=4096;if(Warnings.Capacity>128)Warnings.Capacity=128;
        Volatile.Write(ref _busy,0);
    }
    /// <summary>Releases retained scratch capacity; the workspace may subsequently be reused.</summary>
    public void Dispose()
    {
        if(!Enter())throw new InvalidOperationException("Cannot dispose a workspace during compilation.");
        try{if(_native is not null)NativeInterop.Check(NativeInterop.WorkspaceClear(_native.DangerousGetHandle()));Nodes.Clear();Nodes.Capacity=0;Warnings.Clear();Warnings.Capacity=0;ParserResult?.Dispose();ParserResult=null;}
        finally{Volatile.Write(ref _busy,0);}
    }
}

/// <summary>Serial, explicit native macro sharing. Successful calls commit; errors and cancellation roll back.</summary>
public sealed class MacroSession
{
    /// <summary>Creates an isolated empty serial macro session.</summary>
    public MacroSession() { }
    private int _busy;
    private NativeSessionHandle? _native;
    internal NativeSessionHandle NativeHandle=>_native??=new();
    internal bool Enter()=>Interlocked.CompareExchange(ref _busy,1,0)==0;
    internal void Leave()=>Volatile.Write(ref _busy,0);
}
internal sealed class CompileFailure(MathDiagnostic diagnostic):Exception(diagnostic.Message)
{
    internal MathDiagnostic Diagnostic {get;}=diagnostic;
}
