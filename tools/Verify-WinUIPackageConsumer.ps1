param(
    [string]$FeedDirectory = 'artifacts/packages/p4',
    [string]$OutputDirectory = 'artifacts/p4/package-consumer/winui',
    [ValidatePattern('^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$')][string]$Version = '0.4.0-preview.1',
    [switch]$UpdateLocks
)
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$feed=if([IO.Path]::IsPathRooted($FeedDirectory)){[IO.Path]::GetFullPath($FeedDirectory)}else{[IO.Path]::GetFullPath((Join-Path $repository $FeedDirectory))}
$output=if([IO.Path]::IsPathRooted($OutputDirectory)){[IO.Path]::GetFullPath($OutputDirectory)}else{[IO.Path]::GetFullPath((Join-Path $repository $OutputDirectory))}
# The WinUI XAML compiler still resolves some reference assemblies through
# Windows path APIs. Keep its fresh checkout shallow; reports remain under Output.
$consumer=Join-Path $repository ('artifacts/c-'+[Guid]::NewGuid().ToString('N').Substring(0,8))
$published=Join-Path $consumer 'publish'
$capture=Join-Path $output 'aot'
$lockTemplate=if($Version -eq '0.2.0-preview.1'){Join-Path $repository 'samples/VelociTex.WinUI.Sample/package-consumer.lock.json'}else{Join-Path $repository "samples/VelociTex.WinUI.Sample/package-locks/$Version/packages.lock.json"}
if($UpdateLocks){New-Item -ItemType Directory -Path (Split-Path $lockTemplate -Parent) -Force | Out-Null}
New-Item -ItemType Directory -Path $consumer -Force | Out-Null
$source=Join-Path $repository 'samples/VelociTex.WinUI.Sample'
$sourceHashes=@()
foreach($name in @('FormulaWindow.cs','ControlAcceptance.cs','WindowCapture.cs','App.xaml','app.manifest')){
    $path=Join-Path $source $name
    Copy-Item -LiteralPath $path -Destination (Join-Path $consumer $name)
    $sourceHashes += [ordered]@{path=$name;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
}
Copy-Item -LiteralPath (Join-Path $repository 'global.json') -Destination (Join-Path $consumer 'global.json')
'<Project />' | Set-Content -LiteralPath (Join-Path $consumer 'Directory.Build.props') -Encoding utf8
'<Project />' | Set-Content -LiteralPath (Join-Path $consumer 'Directory.Build.targets') -Encoding utf8
@"
<Project>
  <PropertyGroup><ManagePackageVersionsCentrally>true</ManagePackageVersionsCentrally></PropertyGroup>
  <ItemGroup>
    <PackageVersion Include="VelociTex.Core" Version="$Version" />
    <PackageVersion Include="VelociTex.Rendering.Direct2D" Version="$Version" />
    <PackageVersion Include="VelociTex.WinUI" Version="$Version" />
    <PackageVersion Include="Microsoft.WindowsAppSDK.WinUI" Version="2.3.9" />
    <PackageVersion Include="Microsoft.Windows.SDK.BuildTools" Version="10.0.28000.2705" />
    <PackageVersion Include="Microsoft.Windows.CsWinRT" Version="2.3.1" />
  </ItemGroup>
</Project>
"@ | Set-Content -LiteralPath (Join-Path $consumer 'Directory.Packages.props') -Encoding utf8
$escapedFeed=[Security.SecurityElement]::Escape($feed)
@"
<?xml version="1.0" encoding="utf-8"?>
<configuration><packageSources><clear /><add key="VelociTex-local" value="$escapedFeed" /><add key="nuget.org" value="https://api.nuget.org/v3/index.json" /></packageSources><packageSourceMapping><clear /><packageSource key="VelociTex-local"><package pattern="VelociTex.*" /></packageSource><packageSource key="nuget.org"><package pattern="Microsoft.*" /><package pattern="System.*" /><package pattern="runtime.*" /></packageSource></packageSourceMapping></configuration>
"@ | Set-Content -LiteralPath (Join-Path $consumer 'NuGet.Config') -Encoding utf8
@'
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>WinExe</OutputType><AssemblyName>VelociTex.WinUI.PackageConsumer</AssemblyName>
    <TargetFramework>net10.0-windows10.0.26100.0</TargetFramework><TargetPlatformMinVersion>10.0.19041.0</TargetPlatformMinVersion>
    <WindowsSdkPackageVersion>10.0.26100.87</WindowsSdkPackageVersion><UseWinUI>true</UseWinUI><EnableWindowsTargeting>true</EnableWindowsTargeting>
    <WindowsPackageType>None</WindowsPackageType><EnableMsixTooling>false</EnableMsixTooling><WindowsAppSDKSelfContained>true</WindowsAppSDKSelfContained>
    <SelfContained>true</SelfContained><RuntimeIdentifier>win-x64</RuntimeIdentifier><PlatformTarget>x64</PlatformTarget><Platforms>x64</Platforms>
    <PublishAot>true</PublishAot><IsAotCompatible>true</IsAotCompatible><AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <LangVersion>14</LangVersion><Nullable>enable</Nullable><ImplicitUsings>enable</ImplicitUsings>
    <ApplicationManifest>app.manifest</ApplicationManifest><DefineConstants>$(DefineConstants);DISABLE_XAML_GENERATED_MAIN</DefineConstants>
    <RestorePackagesWithLockFile>true</RestorePackagesWithLockFile><RestorePackagesPath>$(MSBuildProjectDirectory)/packages</RestorePackagesPath>
    <DefaultItemExcludes>$(DefaultItemExcludes);packages/**</DefaultItemExcludes>
    <JsonSerializerIsReflectionEnabledByDefault>false</JsonSerializerIsReflectionEnabledByDefault>
    <WarningsAsErrors>IL2026;IL2057;IL2060;IL2070;IL2072;IL2075;IL2091;IL2104;IL3050;IL3053</WarningsAsErrors>
  </PropertyGroup>
  <ItemGroup>
    <PackageReference Include="VelociTex.Core" />
    <PackageReference Include="VelociTex.Rendering.Direct2D" />
    <PackageReference Include="VelociTex.WinUI" />
    <PackageReference Include="Microsoft.WindowsAppSDK.WinUI" />
    <PackageReference Include="Microsoft.Windows.SDK.BuildTools" PrivateAssets="all" />
    <PackageReference Include="Microsoft.Windows.CsWinRT" PrivateAssets="all" />
  </ItemGroup>
  <Target Name="IncludeNativeXamlPublishResources" BeforeTargets="ComputeFilesToPublish">
    <ItemGroup>
      <_NativeXamlResource Include="$(TargetDir)**\*.xbf" />
      <ResolvedFileToPublish Include="$(TargetDir)$(AssemblyName).pri" Condition="Exists('$(TargetDir)$(AssemblyName).pri')"><RelativePath>$(AssemblyName).pri</RelativePath><CopyToPublishDirectory>PreserveNewest</CopyToPublishDirectory></ResolvedFileToPublish>
      <ResolvedFileToPublish Include="@(_NativeXamlResource)"><RelativePath>%(_NativeXamlResource.RecursiveDir)%(_NativeXamlResource.Filename)%(_NativeXamlResource.Extension)</RelativePath><CopyToPublishDirectory>PreserveNewest</CopyToPublishDirectory></ResolvedFileToPublish>
    </ItemGroup>
    <Error Condition="!Exists('$(TargetDir)$(AssemblyName).pri') Or !Exists('$(TargetDir)App.xbf')" Text="The real WinUI consumer requires its generated PRI and XBF resources." />
  </Target>
</Project>
'@ | Set-Content -LiteralPath (Join-Path $consumer 'Consumer.csproj') -Encoding utf8
@'
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
namespace VelociTex.WinUI.Sample;
internal static class Program
{
    [STAThread]private static int Main(string[] args)
    {
        WinRT.ComWrappersSupport.InitializeComWrappers();
        Application.Start(parameters=>{SynchronizationContext.SetSynchronizationContext(new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread()));_ = new ProbeApp(args);});
        return 0;
    }
}
public sealed partial class ProbeApp:Application
{
    private readonly string[] _arguments;private Window? _window;
    public ProbeApp(string[] args){_arguments=args;InitializeComponent();}
    protected override void OnLaunched(LaunchActivatedEventArgs args){_window=new FormulaWindow(_arguments);_window.Activate();}
}
'@ | Set-Content -LiteralPath (Join-Path $consumer 'Program.cs') -Encoding utf8
Push-Location $consumer
try{
    $env:MSBuildEnableWorkloadResolver='false'
    $restoreArguments=@('restore','Consumer.csproj','-r','win-x64','--configfile','NuGet.Config','--nologo')
    if(!$UpdateLocks){
        if(!(Test-Path -LiteralPath $lockTemplate)){throw 'The reviewed WinUI package-consumer lock is missing. Bootstrap explicitly with -UpdateLocks.'}
        Copy-Item -LiteralPath $lockTemplate -Destination 'packages.lock.json'
        $restoreArguments += '--locked-mode'
    }
    & dotnet @restoreArguments
    if($LASTEXITCODE -ne 0){throw 'Isolated WinUI package restore failed.'}
    if($UpdateLocks){Copy-Item -LiteralPath 'packages.lock.json' -Destination $lockTemplate}
    & dotnet publish Consumer.csproj -c Release -r win-x64 -p:Platform=x64 -p:RestoreLockedMode=true -o $published --nologo
    if($LASTEXITCODE -ne 0){throw 'Isolated WinUI package AOT publish failed.'}
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repository 'tools/Verify-MathViewAutomation.ps1') -ApplicationPath (Join-Path $published 'VelociTex.WinUI.PackageConsumer.exe') -OutputDirectory $capture
    if($LASTEXITCODE -ne 0){throw 'Isolated WinUI package live acceptance failed.'}
    $liveReport=Get-Content -LiteralPath (Join-Path $capture 'winui-report.json') -Raw | ConvertFrom-Json
    if(!$liveReport.nativeAot){throw 'The isolated WinUI package consumer did not execute as Native AOT.'}
    foreach($item in $sourceHashes){if((Get-FileHash -LiteralPath (Join-Path $source $item.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $item.sha256){throw "WinUI sample changed while the isolated consumer was running: $($item.path)"}}
    $assets=Get-Content -LiteralPath 'obj/project.assets.json' -Raw | ConvertFrom-Json -AsHashtable
    if(@($assets.libraries.Values | Where-Object {$_.type -eq 'project'}).Count -ne 0){throw 'The isolated consumer unexpectedly used a ProjectReference.'}
    $packageHashes=@()
    foreach($id in @('VelociTex.Core','VelociTex.Rendering.Direct2D','VelociTex.WinUI')){
        $nupkg=Join-Path $feed "$id.$Version.nupkg"
        $restoredNupkg=Join-Path $consumer ('packages/'+$id.ToLowerInvariant()+"/$Version/"+$id.ToLowerInvariant()+".$Version.nupkg")
        $feedHash=(Get-FileHash -LiteralPath $nupkg -Algorithm SHA256).Hash.ToLowerInvariant()
        $restoredHash=(Get-FileHash -LiteralPath $restoredNupkg -Algorithm SHA256).Hash.ToLowerInvariant()
        if($feedHash -ne $restoredHash){throw "The restored $id payload differs from the selected local feed."}
        $dll=Get-ChildItem -LiteralPath (Join-Path $consumer ('packages/'+$id.ToLowerInvariant()+"/$Version/lib")) -Filter "$id.dll" -Recurse
        if(@($dll).Count -ne 1){throw "Expected exactly one restored $id assembly."}
        $packageHashes += [ordered]@{id=$id;nupkgSha256=$feedHash;restoredNupkgSha256=$restoredHash;restoredAssemblySha256=(Get-FileHash -LiteralPath $dll.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    }
    $consumerInputs=@(Get-ChildItem -LiteralPath $consumer -File | ForEach-Object{[ordered]@{path=$_.Name;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})
    $publishedHashes=@(Get-ChildItem -LiteralPath $published -File -Recurse | ForEach-Object{[ordered]@{path=[IO.Path]::GetRelativePath($published,$_.FullName).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})
    $report=[ordered]@{scope='Isolated local PackageReference WinUI Native AOT consumer, independent UIA and actual keyboard/window captures';version=$Version;consumer=$consumer;feed=$feed;noProjectReferences=$true;privatePackageCache=$true;nativeAot=$true;packages=$packageHashes;sampleSources=$sourceHashes;consumerInputs=$consumerInputs;publishedFiles=$publishedHashes;sourceLinkVerified=$false;screenReaderAudioTested=$false}
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $output 'package-consumer-report.json') -Encoding utf8
    Write-Host "Isolated WinUI local package consumer passed; report in $output."
}
finally{Pop-Location}
