using System.IO.Compression;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Security.Cryptography;
using System.Text.Json;

if (args.Length < 3) throw new ArgumentException("Usage: <package-directory> <version> <output-json>");
string directory = Path.GetFullPath(args[0]), version = args[1], destination = Path.GetFullPath(args[2]);
var sourceLinkKind = new Guid("CC110556-A091-4D38-9FEC-25AB9A351A6A");
var embeddedSourceKind = new Guid("0E8A571B-6926-466E-B4AD-8AB04611F5FE");
var sha256Kind = new Guid("8829D00F-11B8-4213-878B-770E8597AC16");
var sha1Kind = new Guid("FF1816EC-AA5E-4D10-87F7-6F4963833460");
var packages = new List<PackageInspection>();
foreach (string id in new[] { "VelociTex.Core", "VelociTex.Rendering.Direct2D", "VelociTex.WinUI" })
{
    string symbols = Path.Combine(directory, id + "." + version + ".snupkg");
    using var archive = ZipFile.OpenRead(symbols);
    var entries = archive.Entries.Where(e => e.Name.Equals(id + ".pdb", StringComparison.Ordinal)).ToArray();
    if (entries.Length != 1) throw new InvalidDataException("Expected one portable PDB in " + symbols);
    using var pdb = new MemoryStream(); using (var entry = entries[0].Open()) entry.CopyTo(pdb); pdb.Position = 0;
    using var provider = MetadataReaderProvider.FromPortablePdbStream(pdb, MetadataStreamOptions.LeaveOpen);
    var metadata = provider.GetMetadataReader(); string? sourceLink = null;
    foreach (var handle in metadata.GetCustomDebugInformation(MetadataTokens.EntityHandle(0x00000001)))
    {
        var info = metadata.GetCustomDebugInformation(handle);
        if (metadata.GetGuid(info.Kind) == sourceLinkKind) sourceLink = System.Text.Encoding.UTF8.GetString(metadata.GetBlobBytes(info.Value));
    }
    if (sourceLink is null) throw new InvalidDataException("The actual portable PDB has no SourceLink record: " + symbols);
    using var sourceMap = JsonDocument.Parse(sourceLink);
    var mappings = sourceMap.RootElement.GetProperty("documents").EnumerateObject().ToDictionary(p => p.Name, p => p.Value.GetString()!, StringComparer.Ordinal);
    if (mappings.Count == 0 || mappings.Values.Any(v => !v.StartsWith("https://raw.githubusercontent.com/TaiRuiQu/VelociTex/", StringComparison.OrdinalIgnoreCase) || !v.EndsWith("/*", StringComparison.Ordinal)))
        throw new InvalidDataException("SourceLink does not map to the user-confirmed repository: " + sourceLink);
    var documents = new List<DocumentInspection>();
    foreach (var handle in metadata.Documents)
    {
        var document = metadata.GetDocument(handle); string name = metadata.GetString(document.Name);
        string checksum = Convert.ToHexStringLower(metadata.GetBlobBytes(document.Hash));
        byte[]? embedded = null;
        foreach (var customHandle in metadata.GetCustomDebugInformation(handle))
        {
            var info = metadata.GetCustomDebugInformation(customHandle); if (metadata.GetGuid(info.Kind) != embeddedSourceKind) continue;
            var bytes = metadata.GetBlobBytes(info.Value); if (bytes.Length < 4) throw new InvalidDataException("Malformed embedded source record.");
            int uncompressed = BitConverter.ToInt32(bytes, 0);
            if (uncompressed == 0) embedded = bytes[4..];
            else
            {
                using var compressed = new MemoryStream(bytes, 4, bytes.Length - 4); using var deflate = new DeflateStream(compressed, CompressionMode.Decompress); using var restored = new MemoryStream();
                deflate.CopyTo(restored); embedded = restored.ToArray(); if (embedded.Length != uncompressed) throw new InvalidDataException("Embedded source length differs.");
            }
        }
        bool? checksumMatches = null;
        if (embedded is not null)
        {
            var algorithm = metadata.GetGuid(document.HashAlgorithm);
            checksumMatches = algorithm == sha256Kind ? Convert.ToHexStringLower(SHA256.HashData(embedded)) == checksum : algorithm == sha1Kind ? Convert.ToHexStringLower(SHA1.HashData(embedded)) == checksum : null;
            if (checksumMatches == false) throw new InvalidDataException("Embedded source checksum mismatch: " + name);
        }
        documents.Add(new(name, checksum, embedded is not null, embedded?.Length ?? 0, checksumMatches));
    }
    var ownedSources = documents.Where(d => d.Name.EndsWith(".cs", StringComparison.OrdinalIgnoreCase) && !d.Name.Contains("/obj/", StringComparison.OrdinalIgnoreCase) && !d.Name.Contains("\\obj\\", StringComparison.OrdinalIgnoreCase)).ToArray();
    if (ownedSources.Length == 0 || ownedSources.Any(d => !d.Embedded || d.ChecksumMatches != true))
        throw new InvalidDataException("Every owned C# source must be embedded and checksum verified in " + symbols);
    using var payloadHashes = new MemoryStream();
    foreach (var document in ownedSources.OrderBy(d => d.Name, StringComparer.Ordinal))
    { var bytes = System.Text.Encoding.UTF8.GetBytes(document.Name + "\0" + document.Checksum + "\n"); payloadHashes.Write(bytes); }
    var payloadSourceHash = Convert.ToHexStringLower(SHA256.HashData(payloadHashes.ToArray()));
    packages.Add(new(id, symbols, Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(symbols))), entries[0].FullName, Convert.ToHexStringLower(SHA256.HashData(pdb.ToArray())), mappings, documents, payloadSourceHash));
}
Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
using (var output = File.Create(destination))
using (var json = new Utf8JsonWriter(output, new() { Indented = true }))
{
    json.WriteStartObject(); json.WriteNumber("schemaVersion", 1); json.WriteString("version", version); json.WriteBoolean("sourceLinkSyntaxVerified", true);
    json.WriteBoolean("remoteSourceAvailabilityVerified", false); json.WriteString("boundary", "Actual portable PDB SourceLink records and embedded source checksums are inspected. A local/unpublished revision is not claimed remotely accessible.");
    json.WriteStartArray("packages"); foreach (var package in packages)
    {
        json.WriteStartObject(); json.WriteString("id", package.Id); json.WriteString("symbolsPackage", package.SymbolsPackage); json.WriteString("symbolsSha256", package.SymbolsSha256); json.WriteString("pdbEntry", package.PdbEntry); json.WriteString("pdbSha256", package.PdbSha256);
        json.WriteStartObject("sourceLinkDocuments"); foreach (var mapping in package.Mappings) json.WriteString(mapping.Key, mapping.Value); json.WriteEndObject();
        json.WriteString("embeddedOwnedSourceAggregateSha256", package.PayloadSourceHash);
        json.WriteStartArray("sourceMapRevisions"); foreach (var revision in package.Mappings.Values.Select(v => v.Split('/')[5]).Distinct()) json.WriteStringValue(revision); json.WriteEndArray();
        json.WriteBoolean("allOwnedCSharpSourcesEmbeddedAndChecksumVerified", true);
        json.WriteStartArray("documents"); foreach (var document in package.Documents)
        { json.WriteStartObject(); json.WriteString("name", document.Name); json.WriteString("checksum", document.Checksum); json.WriteBoolean("embedded", document.Embedded); json.WriteNumber("embeddedBytes", document.EmbeddedBytes); if (document.ChecksumMatches is { } equal) json.WriteBoolean("embeddedChecksumMatches", equal); else json.WriteNull("embeddedChecksumMatches"); json.WriteEndObject(); }
        json.WriteEndArray(); json.WriteEndObject();
    } json.WriteEndArray(); json.WriteEndObject();
}
Console.WriteLine("Inspected actual SourceLink records in " + packages.Count + " portable PDBs: " + destination);
internal sealed record DocumentInspection(string Name, string Checksum, bool Embedded, int EmbeddedBytes, bool? ChecksumMatches);
internal sealed record PackageInspection(string Id, string SymbolsPackage, string SymbolsSha256, string PdbEntry, string PdbSha256, Dictionary<string, string> Mappings, List<DocumentInspection> Documents, string PayloadSourceHash);
