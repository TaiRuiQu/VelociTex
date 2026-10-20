namespace VelociTex;

/// <summary>Stateless, thread-safe formula compiler. Use a distinct workspace per concurrent call.</summary>
public sealed partial class MathEngine
{
    /// <summary>Compiles with bounded defaults and a temporary isolated workspace.</summary>
    public bool TryCompile(string latex,out MathDocument? document,out MathDiagnostic diagnostic)
    { using var workspace=new MathWorkspace();return TryCompile(latex,new MathOptions(),workspace,default,out document,out diagnostic); }

    /// <summary>Validates, expands, parses, shapes and lays out a complete formula synchronously in the native engine.</summary>
    public bool TryCompile(string latex,in MathOptions options,MathWorkspace workspace,CancellationToken cancellationToken,out MathDocument? document,out MathDiagnostic diagnostic)
        => CompileNative(latex,in options,workspace,cancellationToken,true,null,out document,out diagnostic,out _);

    /// <summary>Compiles presentation MathML without allocating layout or drawing data.</summary>
    public bool TryCompileToMathMl(string latex,in MathOptions options,MathWorkspace workspace,CancellationToken cancellationToken,out string? mathml,out MathWarning[] warnings,out MathDiagnostic diagnostic)
    {
        mathml=null;warnings=[];
        if(!CompileNative(latex,in options,workspace,cancellationToken,false,null,out var document,out diagnostic,out _))return false;
        try {mathml=MathMlWriter.Write(document!);warnings=document!.Warnings.ToArray();return true;}
        finally {document!.NativeHandle.Dispose();}
    }

    internal static unsafe bool CompileNative(string source,in MathOptions options,MathWorkspace workspace,CancellationToken token,bool layout,MathPresentationOptions? presentation,out MathDocument? document,out MathDiagnostic diagnostic,out bool fallback)
    {
        ArgumentNullException.ThrowIfNull(source);ArgumentNullException.ThrowIfNull(workspace);
        document=null;diagnostic=default;fallback=false;
        if(!Valid(options)){diagnostic=new(MathDiagnosticCode.InvalidOptions,new(0,0),"Options exceed supported resource or font-size bounds.");return false;}
        if(source.Length>options.MaxInputLength){diagnostic=new(MathDiagnosticCode.InputLimit,new(options.MaxInputLength,source.Length-options.MaxInputLength),"Input length budget exceeded.");return false;}
        if(!workspace.Enter()){diagnostic=new(MathDiagnosticCode.WorkspaceInUse,new(0,0),"Workspace is already in use.");return false;}
        bool sessionEntered=false;
        try
        {
            if(token.IsCancellationRequested){diagnostic=new(MathDiagnosticCode.Cancelled,new(0,source.Length),"Compilation was cancelled.");return false;}
            if(options.Session is not null&&!(sessionEntered=options.Session.Enter())){diagnostic=new(MathDiagnosticCode.SessionInUse,new(0,0),"Macro session is already in use.");return false;}
            using var invocation=new NativeInterop.Invocation(in options,token);
            nint pointer=0;int status;NativeInterop.Diagnostic nativeDiagnostic;
            fixed(char* input=source)fixed(NativeInterop.Options* settings=&invocation.Options)
            {
                var text=new NativeInterop.String16{Data=input,Length=source.Length};
                if(presentation is {} p){var c=p.ErrorColor;uint rgba=(uint)c.R<<24|(uint)c.G<<16|(uint)c.B<<8|c.A;status=NativeInterop.Present(workspace.NativeHandle.DangerousGetHandle(),text,settings,p.RenderErrors?1:0,rgba,out pointer,out nativeDiagnostic,out int isFallback);fallback=isFallback!=0;}
                else status=NativeInterop.Compile(workspace.NativeHandle.DangerousGetHandle(),text,settings,layout?1:0,out pointer,out nativeDiagnostic);
            }
            NativeResultHandle? result=pointer==0?null:new(pointer);
            try
            {
                // The native parser probes strict behavior with report=false in a few
                // compatibility paths. It intentionally catches a handler exception
                // there and continues with Error semantics. Status 1 carries that
                // structured compile diagnostic; callback-failure statuses are the
                // ones that should rethrow the original managed exception.
                if(status is not 0 and not 1) invocation.RethrowCallback();
                if(status is 0 or 1)diagnostic=nativeDiagnostic.Value;
                if(status==1)return false;
                NativeInterop.Check(status);
                document=new(source,in options,result!);result=null;return true;
            }
            finally {result?.Dispose();GC.KeepAlive(workspace);}
        }
        finally {if(sessionEntered)options.Session!.Leave();workspace.Leave();}
    }
    private static bool Valid(in MathOptions o)=>double.IsFinite(o.FontSize)&&o.FontSize is >0 and <=1024&&o.MaxInputLength is >0 and <=1_048_576&&o.MaxNodes is >0 and <=262_144&&o.MaxNesting is >0 and <=1024&&o.MaxExpansion is >0 and <=1_000_000&&o.MaxTokens is >0 and <=1_048_576&&!double.IsNaN(o.MaxSize)&&o.MaxSize>=0&&double.IsFinite(o.MinRuleThickness)&&o.MinRuleThickness>=0&&double.IsFinite(o.DisplayWidth)&&o.DisplayWidth is >=0 and <=32768&&o.EquationNumber is >0 and <=1_000_000&&Enum.IsDefined(o.StrictMode);
}
