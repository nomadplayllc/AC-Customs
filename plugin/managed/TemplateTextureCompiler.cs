// AC Customs: Master Template PNG compiler, Phase 1 core.
// Targets the existing .NET Framework 4.8 x86 managed plugin, with no new dependencies.
// Deliberately contains NO game hooks, UI controls, or pack-switching logic.
// Produces the same raw .rgb files and manifest.json layout as an installed ACUI pack.
// The plugin's default path compiles every mapped region from an arbitrary PNG
// and its user-selected mapping JSON. No original/baseline master is required.
// The legacy comparison-based API is retained for compatibility.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace ACCustoms
{
    [DataContract]
    internal sealed class TemplateMappingFile
    {
        [DataMember(Name = "formatVersion")] public int FormatVersion { get; set; }
        [DataMember(Name = "width")] public int Width { get; set; }
        [DataMember(Name = "height")] public int Height { get; set; }
        [DataMember(Name = "pixelCrc32")] public string PixelCrc32 { get; set; }
        [DataMember(Name = "textures")] public List<TemplateMappingEntry> Textures { get; set; }
    }

    [DataContract]
    internal sealed class TemplateMappingEntry
    {
        [DataMember(Name = "did")] public string Did { get; set; }
        [DataMember(Name = "x")] public int X { get; set; }
        [DataMember(Name = "y")] public int Y { get; set; }
        [DataMember(Name = "width")] public int Width { get; set; }
        [DataMember(Name = "height")] public int Height { get; set; }
        [DataMember(Name = "pixelFormat")] public string PixelFormat { get; set; }
        [DataMember(Name = "method")] public string Method { get; set; }
        [IgnoreDataMember] public int BytesPerPixel { get; set; }
    }

    [DataContract]
    internal sealed class TemplateOutputReceipt
    {
        [DataMember(Name = "did")] public string Did { get; set; }
        [DataMember(Name = "sha256")] public string Sha256 { get; set; }
    }

    [DataContract]
    internal sealed class TemplateBuildReceipt
    {
        [DataMember(Name = "compilerVersion")] public string CompilerVersion { get; set; }
        [DataMember(Name = "sourcePath")] public string SourcePath { get; set; }
        [DataMember(Name = "sourceSha256")] public string SourceSha256 { get; set; }
        [DataMember(Name = "originalSha256")] public string OriginalSha256 { get; set; }
        [DataMember(Name = "mappingSha256")] public string MappingSha256 { get; set; }
        [DataMember(Name = "generatedUtc")] public string GeneratedUtc { get; set; }
        [DataMember(Name = "mappedCount")] public int MappedCount { get; set; }
        [DataMember(Name = "changedCount")] public int ChangedCount { get; set; }
        [DataMember(Name = "outputs")] public List<TemplateOutputReceipt> Outputs { get; set; }
    }

    internal enum TemplateBuildState
    {
        NotGenerated,
        UpToDate,
        RegenerationRequired,
        Invalid
    }

    internal sealed class TemplateBuildReport
    {
        public int MappedCount { get; set; }
        public int ChangedCount { get; set; }
        public int UnchangedCount { get { return MappedCount - ChangedCount; } }
        public string OutputDirectory { get; set; }
        public bool HasReplacementTextures { get { return ChangedCount != 0; } }
    }

    internal static class TemplateTextureCompiler
    {
        internal const string Version = "master-png-v2-full";
        internal const string ReceiptFileName = "template-build.json";
        internal const string ManifestFileName = "manifest.json";

        // The mapping's pixelCrc32 is informational, not a requirement: the
        // chosen PNG and chosen mapping file are the only necessary inputs.
        // In particular, the plugin must not depend on a baked-in master PNG.
        public static TemplateBuildReport GenerateAll(
            string themePng, string mappingJson, string destinationDirectory)
        {
            return Generate(themePng, null, mappingJson, destinationDirectory);
        }

        // Compatibility API: non-empty baseline means changed-region only;
        // null/empty baseline means compile all mapped regions.
        public static TemplateBuildReport Generate(
            string editedPng, string officialOriginalPng, string mappingJson,
            string destinationDirectory)
        {
            if (String.IsNullOrWhiteSpace(destinationDirectory))
                throw new ArgumentException("Output directory is required.");
            string output = Path.GetFullPath(destinationDirectory).TrimEnd(Path.DirectorySeparatorChar);
            string edited = RequireFile(editedPng, "Edited PNG");
            bool compareToOriginal = !String.IsNullOrWhiteSpace(officialOriginalPng);
            string original = compareToOriginal
                ? RequireFile(officialOriginalPng, "Untouched template PNG") : null;
            string mappingPath = RequireFile(mappingJson, "Mapping JSON");

            // Never place source files inside the directory the generator replaces.
            foreach (string p in new[] { edited, original, mappingPath })
                if (p != null && p.StartsWith(output + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                    throw new InvalidOperationException("Output directory contains an input file: " + p);

            TemplateMappingFile mapping = ReadJson<TemplateMappingFile>(mappingPath);
            List<TemplateMappingEntry> entries = ValidateMappings(mapping);
            byte[] editedPixels;
            byte[] originalPixels;
            // All inputs are fully validated/decoded BEFORE creating any output files.
            editedPixels = ReadTemplatePixels(edited);
            originalPixels = compareToOriginal ? ReadTemplatePixels(original) : null;
            string sourceHash = FileHash(edited);
            string originalHash = compareToOriginal ? FileHash(original) : String.Empty;
            string mappingHash = FileHash(mappingPath);

            string parent = Path.GetDirectoryName(output);
            if (String.IsNullOrEmpty(parent))
                throw new ArgumentException("Output must have a parent directory.");
            Directory.CreateDirectory(parent);
            string stage = Path.Combine(parent, ".template-stage-" + Guid.NewGuid().ToString("N"));
            string backup = Path.Combine(parent, ".template-backup-" + Guid.NewGuid().ToString("N"));
            bool backedUp = false;
            bool committed = false;
            int changedCount = 0;
            var outputs = new List<TemplateOutputReceipt>();
            try
            {
                string texturesDir = Path.Combine(stage, "textures");
                Directory.CreateDirectory(texturesDir);
                var manifest = new AcuiManifest
                {
                    FormatVersion = 1,
                    Name = "Custom PNG Theme",
                    Author = "AC Customs",
                    Description = "Compiled from a selected 2000x2000 PNG and mapping JSON",
                    Textures = new List<AcuiTextureEntry>()
                };
                foreach (TemplateMappingEntry entry in entries)
                {
                    if (compareToOriginal && !RegionChanged(editedPixels, originalPixels, entry))
                        continue;
                    byte[] raw = ExtractPixels(editedPixels, entry);
                    string filename = entry.Did + ".rgb";
                    File.WriteAllBytes(Path.Combine(texturesDir, filename), raw);
                    using (SHA256 sha = SHA256.Create())
                        outputs.Add(new TemplateOutputReceipt
                        {
                            Did = entry.Did,
                            Sha256 = BitConverter.ToString(sha.ComputeHash(raw)).Replace("-", "")
                        });
                    manifest.Textures.Add(new AcuiTextureEntry
                    {
                        Did = entry.Did,
                        Width = (uint)entry.Width,
                        Height = (uint)entry.Height,
                        ImageSize = (uint)raw.Length,
                        PixelFormat = entry.BytesPerPixel == 3 ? "0x00000014" : "0x00000015",
                        File = "textures/" + filename
                    });
                    ++changedCount;
                }

                // The optional comparison API may emit no textures when the two
                // images are identical. The baseline-free plugin path always
                // produces one replacement for each valid mapping entry.
                if (changedCount != 0)
                    WriteJson(Path.Combine(stage, ManifestFileName), manifest);
                WriteJson(Path.Combine(stage, ReceiptFileName), new TemplateBuildReceipt
                {
                    CompilerVersion = Version,
                    SourcePath = edited,
                    SourceSha256 = sourceHash,
                    OriginalSha256 = originalHash,
                    MappingSha256 = mappingHash,
                    GeneratedUtc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
                    MappedCount = entries.Count,
                    ChangedCount = changedCount,
                    Outputs = outputs
                });

                // Reject an image/mapping edited while this build was in progress.
                if (FileHash(edited) != sourceHash ||
                    (compareToOriginal && FileHash(original) != originalHash) ||
                    FileHash(mappingPath) != mappingHash)
                    throw new IOException("A source file changed during generation; retry the build.");

                // Never overwrite an in-use generated texture directory. The caller
                // MUST first restore vanilla if this output is the active UI.
                if (Directory.Exists(output))
                {
                    Directory.Move(output, backup);
                    backedUp = true;
                }
                Directory.Move(stage, output);
                committed = true;
                return new TemplateBuildReport
                {
                    MappedCount = entries.Count,
                    ChangedCount = changedCount,
                    OutputDirectory = output
                };
            }
            finally
            {
                if (!committed && backedUp && !Directory.Exists(output))
                    Directory.Move(backup, output);
                if (Directory.Exists(stage))
                    TryDeleteDirectory(stage);
                if (committed && Directory.Exists(backup))
                    TryDeleteDirectory(backup);
            }
        }

        public static TemplateBuildState CheckStatusAll(
            string themePng, string mappingJson,
            string destinationDirectory, out string detail)
        {
            return CheckStatus(themePng, null, mappingJson, destinationDirectory, out detail);
        }

        public static TemplateBuildState CheckStatus(
            string editedPng, string officialOriginalPng, string mappingJson,
            string destinationDirectory, out string detail)
        {
            try
            {
                string receiptPath = Path.Combine(destinationDirectory, ReceiptFileName);
                if (!File.Exists(receiptPath))
                {
                    detail = "No successful template build exists.";
                    return TemplateBuildState.NotGenerated;
                }
                TemplateBuildReceipt r = ReadJson<TemplateBuildReceipt>(receiptPath);
                bool compareToOriginal = !String.IsNullOrWhiteSpace(officialOriginalPng);
                if (r == null || r.CompilerVersion != Version ||
                    !String.Equals(r.SourceSha256, FileHash(editedPng), StringComparison.OrdinalIgnoreCase) ||
                    !String.Equals(r.OriginalSha256,
                        compareToOriginal ? FileHash(officialOriginalPng) : String.Empty,
                        StringComparison.OrdinalIgnoreCase) ||
                    !String.Equals(r.MappingSha256, FileHash(mappingJson), StringComparison.OrdinalIgnoreCase) ||
                    !String.Equals(r.SourcePath, Path.GetFullPath(editedPng), StringComparison.OrdinalIgnoreCase))
                {
                    detail = "Theme PNG, mapping data, output format, or compiler version changed.";
                    return TemplateBuildState.RegenerationRequired;
                }
                string textureDir = Path.Combine(destinationDirectory, "textures");
                if (!Directory.Exists(textureDir) ||
                    Directory.GetFiles(textureDir, "*.rgb").Length != r.ChangedCount)
                {
                    detail = "Generated texture files are missing or the count has changed.";
                    return TemplateBuildState.RegenerationRequired;
                }
                if (!compareToOriginal && (r.MappedCount != r.ChangedCount || r.MappedCount <= 0))
                {
                    detail = "The build did not include every mapped texture.";
                    return TemplateBuildState.RegenerationRequired;
                }
                if (r.Outputs == null || r.Outputs.Count != r.ChangedCount)
                {
                    detail = "Output checksums are missing from the build receipt.";
                    return TemplateBuildState.RegenerationRequired;
                }
                foreach (TemplateOutputReceipt o in r.Outputs)
                {
                    if (o == null || o.Did == null || !Regex.IsMatch(o.Did, "\\A[0-9a-fA-F]{8}\\z") ||
                        !String.Equals(FileHash(Path.Combine(textureDir, o.Did + ".rgb")),
                            o.Sha256, StringComparison.OrdinalIgnoreCase))
                    {
                        detail = "Generated texture content has changed: " + (o == null ? "(null)" : o.Did);
                        return TemplateBuildState.RegenerationRequired;
                    }
                }
                if (r.ChangedCount > 0)
                {
                    string manifestPath = Path.Combine(destinationDirectory, ManifestFileName);
                    if (!File.Exists(manifestPath))
                    {
                        detail = "Generated manifest is missing.";
                        return TemplateBuildState.RegenerationRequired;
                    }
                    AcuiManifest manifest = ReadJson<AcuiManifest>(manifestPath);
                    if (manifest == null || manifest.Textures == null || manifest.Textures.Count != r.ChangedCount)
                    {
                        detail = "Generated manifest is incomplete.";
                        return TemplateBuildState.RegenerationRequired;
                    }
                    foreach (AcuiTextureEntry e in manifest.Textures)
                    {
                        string file = Path.Combine(textureDir, e.Did + ".rgb");
                        if (!File.Exists(file) || (ulong)new FileInfo(file).Length != e.ImageSize)
                        {
                            detail = "Generated texture missing or wrong size: " + e.Did;
                            return TemplateBuildState.RegenerationRequired;
                        }
                    }
                }
                detail = r.ChangedCount == 0
                    ? "Up to date; no replacement textures."
                    : "Up to date; " + r.ChangedCount + " replacement textures are ready.";
                return TemplateBuildState.UpToDate;
            }
            catch (Exception ex)
            {
                detail = ex.Message;
                return TemplateBuildState.Invalid;
            }
        }

        private static List<TemplateMappingEntry> ValidateMappings(TemplateMappingFile file)
        {
            if (file == null || file.FormatVersion != 1 || file.Width != 2000 || file.Height != 2000 ||
                file.Textures == null || file.Textures.Count == 0)
                throw new InvalidDataException("Invalid v1 2000x2000 mapping JSON.");
            HashSet<string> dids = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (TemplateMappingEntry e in file.Textures)
            {
                if (e == null || e.Did == null || !Regex.IsMatch(e.Did, "\\A[0-9a-fA-F]{8}\\z") ||
                    !dids.Add(e.Did) || e.X < 0 || e.Y < 0 || e.Width <= 0 || e.Height <= 0 ||
                    e.X > 2000 - e.Width || e.Y > 2000 - e.Height)
                    throw new InvalidDataException("Invalid or duplicate template mapping DID/rectangle: " +
                        (e == null ? "(null)" : e.Did));
                if (String.Equals(e.PixelFormat, "0x00000014", StringComparison.OrdinalIgnoreCase))
                    e.BytesPerPixel = 3;
                else if (String.Equals(e.PixelFormat, "0x00000015", StringComparison.OrdinalIgnoreCase))
                    e.BytesPerPixel = 4;
                else
                    throw new InvalidDataException("Unsupported pixel format for " + e.Did);
                e.Did = e.Did.ToUpperInvariant();
            }
            return file.Textures.OrderBy(e => e.Did, StringComparer.Ordinal).ToList();
        }

        private static string RequireFile(string path, string label)
        {
            if (String.IsNullOrWhiteSpace(path))
                throw new ArgumentException(label + " is required.");
            string full = Path.GetFullPath(path);
            if (!File.Exists(full))
                throw new FileNotFoundException(label + " was not found.", full);
            return full;
        }

        // A 32bpp ARGB LockBits buffer on little-endian Windows is packed BGRA,
        // which is the native 0x15 texture ordering. This is STRAIGHT alpha,
        // not 32bppPArgb/premultiplied alpha.
        private static byte[] ReadTemplatePixels(string pngPath)
        {
            // Require PNG bytes, not merely a file with a .png extension.
            using (FileStream stream = File.OpenRead(pngPath))
            {
                byte[] signature = new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 };
                foreach (byte b in signature)
                    if (stream.ReadByte() != b)
                        throw new InvalidDataException("Input is not a valid PNG: " + pngPath);
            }
            using (Bitmap source = new Bitmap(pngPath))
            {
                if (source.Width != 2000 || source.Height != 2000)
                    throw new InvalidDataException("Theme PNG must be exactly 2000x2000: " + pngPath);
                // LockBits does not convert arbitrary indexed/RGB bitmap formats.
                // Normalize only when necessary; preserve original 32bpp ARGB
                // bytes where possible to avoid touching transparent RGB colors.
                Bitmap normalized = null;
                Bitmap pixelsBitmap = source;
                if (source.PixelFormat != PixelFormat.Format32bppArgb)
                {
                    normalized = new Bitmap(2000, 2000, PixelFormat.Format32bppArgb);
                    using (Graphics g = Graphics.FromImage(normalized))
                    {
                        g.CompositingMode = System.Drawing.Drawing2D.CompositingMode.SourceCopy;
                        g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.NearestNeighbor;
                        g.DrawImageUnscaled(source, 0, 0);
                    }
                    pixelsBitmap = normalized;
                }
                try
                {
                    Rectangle r = new Rectangle(0, 0, 2000, 2000);
                    BitmapData data = pixelsBitmap.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                    try
                    {
                        const int rowBytes = 2000 * 4;
                        byte[] pixels = new byte[rowBytes * 2000];
                        for (int y = 0; y < 2000; ++y)
                            Marshal.Copy(IntPtr.Add(data.Scan0, y * data.Stride), pixels, y * rowBytes, rowBytes);
                        return pixels;
                    }
                    finally { pixelsBitmap.UnlockBits(data); }
                }
                finally { if (normalized != null) normalized.Dispose(); }
            }
        }

        private static bool RegionChanged(byte[] edited, byte[] original, TemplateMappingEntry e)
        {
            for (int y = 0; y < e.Height; ++y)
            {
                int start = ((e.Y + y) * 2000 + e.X) * 4;
                for (int x = 0; x < e.Width; ++x)
                {
                    int at = start + x * 4;
                    if (edited[at] != original[at] || edited[at + 1] != original[at + 1] ||
                        edited[at + 2] != original[at + 2] ||
                        (e.BytesPerPixel == 4 && edited[at + 3] != original[at + 3]))
                        return true;
                }
            }
            return false;
        }

        private static byte[] ExtractPixels(byte[] pixels, TemplateMappingEntry e)
        {
            byte[] result = new byte[checked(e.Width * e.Height * e.BytesPerPixel)];
            int target = 0;
            for (int y = 0; y < e.Height; ++y)
            {
                int source = ((e.Y + y) * 2000 + e.X) * 4;
                for (int x = 0; x < e.Width; ++x)
                {
                    result[target++] = pixels[source++]; // B
                    result[target++] = pixels[source++]; // G
                    result[target++] = pixels[source++]; // R
                    if (e.BytesPerPixel == 4) result[target++] = pixels[source]; // A
                    ++source;
                }
            }
            return result;
        }

        private static string FileHash(string path)
        {
            using (var stream = File.OpenRead(path))
            using (SHA256 sha = SHA256.Create())
            {
                byte[] hash = sha.ComputeHash(stream);
                return BitConverter.ToString(hash).Replace("-", "");
            }
        }

        private static T ReadJson<T>(string path)
        {
            using (FileStream stream = File.OpenRead(path))
                return (T)new DataContractJsonSerializer(typeof(T)).ReadObject(stream);
        }

        private static void WriteJson<T>(string path, T data)
        {
            using (FileStream stream = File.Create(path))
                new DataContractJsonSerializer(typeof(T)).WriteObject(stream, data);
        }

        private static void TryDeleteDirectory(string path)
        {
            try { Directory.Delete(path, true); }
            catch (IOException) { /* an older inactive backup may require later cleanup */ }
            catch (UnauthorizedAccessException) { }
        }
    }
}
