using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.Cryptography;
using System.Text;

namespace ACCustoms
{
    [DataContract]
    internal sealed class AcuiManifest
    {
        [DataMember(Name = "formatVersion")]
        public int FormatVersion { get; set; }

        [DataMember(Name = "name")]
        public string Name { get; set; }

        [DataMember(Name = "author")]
        public string Author { get; set; }

        [DataMember(Name = "description")]
        public string Description { get; set; }

        [DataMember(Name = "textures")]
        public List<AcuiTextureEntry> Textures { get; set; }
    }

    [DataContract]
    internal sealed class AcuiTextureEntry
    {
        [DataMember(Name = "did")]
        public string Did { get; set; }

        [DataMember(Name = "width")]
        public uint Width { get; set; }

        [DataMember(Name = "height")]
        public uint Height { get; set; }

        [DataMember(Name = "imageSize")]
        public uint ImageSize { get; set; }

        [DataMember(Name = "pixelFormat")]
        public string PixelFormat { get; set; }

        [DataMember(Name = "file")]
        public string File { get; set; }
    }

    internal sealed class InstalledPack
    {
        public string InstallId { get; set; }
        public string Name { get; set; }
        public string Author { get; set; }
        public string Description { get; set; }
        public int TextureCount { get; set; }
        public string RootDirectory { get; set; }
        public string TexturesDirectory { get; set; }

        public string DisplayName
        {
            get
            {
                return String.IsNullOrWhiteSpace(Author)
                    ? Name
                    : Name + " - " + Author;
            }
        }
    }

    internal sealed class PackInspection
    {
        public string SourcePath { get; set; }
        public string InstallId { get; set; }
        public AcuiManifest Manifest { get; set; }
        public bool AlreadyInstalled { get; set; }
    }

    internal static class PackManager
    {
        private static readonly string BaseDirectory = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ACCustoms");

        public static readonly string PacksDirectory = Path.Combine(BaseDirectory, "Packs");
        public static readonly string RuntimeDirectory = Path.Combine(BaseDirectory, "Runtime");
        public static readonly string CaptureDirectory = Path.Combine(RuntimeDirectory, "Captured");
        public static readonly string SnapshotsDirectory = Path.Combine(RuntimeDirectory, "Snapshots");
        public static readonly string EmptyThemeDirectory = Path.Combine(RuntimeDirectory, "EmptyTheme");
        private static readonly string SettingsPath = Path.Combine(BaseDirectory, "settings.txt");

        public static void EnsureDirectories()
        {
            Directory.CreateDirectory(BaseDirectory);
            Directory.CreateDirectory(PacksDirectory);
            Directory.CreateDirectory(RuntimeDirectory);
            Directory.CreateDirectory(CaptureDirectory);
            Directory.CreateDirectory(SnapshotsDirectory);
            Directory.CreateDirectory(EmptyThemeDirectory);
        }

        public static List<InstalledPack> GetInstalledPacks()
        {
            EnsureDirectories();
            List<InstalledPack> result = new List<InstalledPack>();

            foreach (string directory in Directory.GetDirectories(PacksDirectory))
            {
                try
                {
                    string manifestPath = Path.Combine(directory, "manifest.json");
                    string texturesPath = Path.Combine(directory, "textures");
                    if (!File.Exists(manifestPath) || !Directory.Exists(texturesPath))
                        continue;

                    AcuiManifest manifest = ReadManifest(File.ReadAllBytes(manifestPath));
                    ValidateManifestShape(manifest);

                    string installId = Path.GetFileName(directory);
                    result.Add(new InstalledPack
                    {
                        InstallId = installId,
                        Name = manifest.Name,
                        Author = manifest.Author ?? String.Empty,
                        Description = manifest.Description ?? String.Empty,
                        TextureCount = manifest.Textures.Count,
                        RootDirectory = directory,
                        TexturesDirectory = texturesPath
                    });
                }
                catch
                {
                    // A damaged manually-edited directory is ignored rather than
                    // preventing all valid installed packs from loading.
                }
            }

            return result
                .OrderBy(p => p.Name, StringComparer.OrdinalIgnoreCase)
                .ThenBy(p => p.Author, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }

        public static PackInspection InspectPackage(string sourcePath)
        {
            if (String.IsNullOrWhiteSpace(sourcePath) || !File.Exists(sourcePath))
                throw new FileNotFoundException("The selected .acui file does not exist.", sourcePath);

            using (FileStream input = File.Open(sourcePath, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (ZipArchive archive = new ZipArchive(input, ZipArchiveMode.Read, false))
            {
                ZipArchiveEntry manifestEntry = FindEntry(archive, "manifest.json");
                if (manifestEntry == null)
                    throw new InvalidDataException("The selected .acui does not contain manifest.json.");

                AcuiManifest manifest = ReadManifest(ReadEntryBytes(manifestEntry, 4 * 1024 * 1024));
                ValidateManifestShape(manifest);
                ValidateArchiveTextures(archive, manifest);

                string installId = BuildInstallId(manifest.Name, manifest.Author);
                return new PackInspection
                {
                    SourcePath = sourcePath,
                    InstallId = installId,
                    Manifest = manifest,
                    AlreadyInstalled = Directory.Exists(Path.Combine(PacksDirectory, installId))
                };
            }
        }

        public static InstalledPack InstallPackage(PackInspection inspection, bool overwrite)
        {
            if (inspection == null || inspection.Manifest == null)
                throw new ArgumentNullException("inspection");

            EnsureDirectories();
            string destination = Path.Combine(PacksDirectory, inspection.InstallId);
            if (Directory.Exists(destination) && !overwrite)
                throw new IOException("A UI pack with this name and author is already installed.");

            string stage = Path.Combine(PacksDirectory, ".import-" + Guid.NewGuid().ToString("N"));
            string backup = destination + ".backup-" + Guid.NewGuid().ToString("N");

            try
            {
                Directory.CreateDirectory(stage);
                Directory.CreateDirectory(Path.Combine(stage, "textures"));

                using (FileStream input = File.Open(inspection.SourcePath, FileMode.Open, FileAccess.Read, FileShare.Read))
                using (ZipArchive archive = new ZipArchive(input, ZipArchiveMode.Read, false))
                {
                    ZipArchiveEntry manifestEntry = FindEntry(archive, "manifest.json");
                    if (manifestEntry == null)
                        throw new InvalidDataException("manifest.json disappeared from the package.");

                    byte[] manifestBytes = ReadEntryBytes(manifestEntry, 4 * 1024 * 1024);
                    AcuiManifest rechecked = ReadManifest(manifestBytes);
                    ValidateManifestShape(rechecked);
                    ValidateArchiveTextures(archive, rechecked);

                    string recheckedId = BuildInstallId(rechecked.Name, rechecked.Author);
                    if (!String.Equals(recheckedId, inspection.InstallId, StringComparison.Ordinal))
                        throw new InvalidDataException("The package changed while it was being imported.");

                    File.WriteAllBytes(Path.Combine(stage, "manifest.json"), manifestBytes);

                    foreach (AcuiTextureEntry texture in rechecked.Textures)
                    {
                        ZipArchiveEntry entry = FindEntry(archive, texture.File);
                        if (entry == null)
                            throw new InvalidDataException("Missing " + texture.File + ".");

                        string output = Path.Combine(stage, "textures", texture.Did + ".rgb");
                        using (Stream source = entry.Open())
                        using (FileStream target = File.Create(output))
                            source.CopyTo(target);

                        if (new FileInfo(output).Length != texture.ImageSize)
                            throw new InvalidDataException("Replacement byte count changed for DID " + texture.Did + ".");
                    }
                }

                File.Copy(inspection.SourcePath, Path.Combine(stage, "package.acui"), true);

                if (Directory.Exists(destination))
                    Directory.Move(destination, backup);

                Directory.Move(stage, destination);

                // Installation is committed once the staging directory has
                // moved into place. Backup cleanup is best-effort and must not
                // turn a successful install into a reported failure.
                try
                {
                    if (Directory.Exists(backup))
                        Directory.Delete(backup, true);
                }
                catch { }

                return new InstalledPack
                {
                    InstallId = inspection.InstallId,
                    Name = inspection.Manifest.Name,
                    Author = inspection.Manifest.Author ?? String.Empty,
                    Description = inspection.Manifest.Description ?? String.Empty,
                    TextureCount = inspection.Manifest.Textures.Count,
                    RootDirectory = destination,
                    TexturesDirectory = Path.Combine(destination, "textures")
                };
            }
            catch
            {
                try
                {
                    if (Directory.Exists(stage))
                        Directory.Delete(stage, true);
                }
                catch { }

                try
                {
                    if (!Directory.Exists(destination) && Directory.Exists(backup))
                        Directory.Move(backup, destination);
                }
                catch { }

                throw;
            }
        }

        public static string LoadLastSelectedInstallId()
        {
            try
            {
                if (!File.Exists(SettingsPath))
                    return String.Empty;

                foreach (string line in File.ReadAllLines(SettingsPath, Encoding.UTF8))
                {
                    const string prefix = "lastSelectedPack=";
                    if (line.StartsWith(prefix, StringComparison.Ordinal))
                        return line.Substring(prefix.Length).Trim();
                }
            }
            catch { }
            return String.Empty;
        }

        public static void SaveLastSelectedInstallId(string installId)
        {
            try
            {
                EnsureDirectories();
                File.WriteAllText(
                    SettingsPath,
                    "lastSelectedPack=" + (installId ?? String.Empty) + Environment.NewLine,
                    new UTF8Encoding(false));
            }
            catch
            {
                // Selection persistence must never affect gameplay.
            }
        }

        private static AcuiManifest ReadManifest(byte[] bytes)
        {
            using (MemoryStream stream = new MemoryStream(bytes, false))
            {
                DataContractJsonSerializer serializer = new DataContractJsonSerializer(typeof(AcuiManifest));
                AcuiManifest manifest = serializer.ReadObject(stream) as AcuiManifest;
                if (manifest == null)
                    throw new InvalidDataException("manifest.json could not be parsed.");
                return manifest;
            }
        }

        private static void ValidateManifestShape(AcuiManifest manifest)
        {
            if (manifest.FormatVersion != 1)
                throw new InvalidDataException("Unsupported ACUI formatVersion. This plugin supports formatVersion 1.");
            if (String.IsNullOrWhiteSpace(manifest.Name))
                throw new InvalidDataException("The ACUI manifest does not contain a pack name.");
            if (manifest.Textures == null || manifest.Textures.Count == 0)
                throw new InvalidDataException("The ACUI pack contains no replacement textures.");

            HashSet<string> seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (AcuiTextureEntry texture in manifest.Textures)
            {
                if (texture == null)
                    throw new InvalidDataException("The ACUI textures array contains an invalid entry.");

                string did = (texture.Did ?? String.Empty).ToUpperInvariant();
                uint didValue;
                if (did.Length != 8 ||
                    !UInt32.TryParse(did, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out didValue) ||
                    (didValue & 0xFF000000u) != 0x06000000u)
                {
                    throw new InvalidDataException("Invalid texture DID in ACUI manifest: " + did);
                }

                if (!seen.Add(did))
                    throw new InvalidDataException("Duplicate texture DID in ACUI manifest: " + did);

                uint pixelFormat = ParseHexUInt(texture.PixelFormat, "pixelFormat for " + did);
                if (pixelFormat != 0x14u && pixelFormat != 0x15u)
                    throw new InvalidDataException("Unsupported replacement format for DID " + did + ".");

                if (texture.Width == 0 || texture.Height == 0 || texture.ImageSize == 0)
                    throw new InvalidDataException("Invalid dimensions or imageSize for DID " + did + ".");

                ulong bytesPerPixel = pixelFormat == 0x15u ? 4ul : 3ul;
                ulong expected = (ulong)texture.Width * (ulong)texture.Height * bytesPerPixel;
                if (expected != texture.ImageSize)
                    throw new InvalidDataException("Inconsistent imageSize for DID " + did + ".");

                string expectedPath = "textures/" + did + ".rgb";
                if (!String.Equals(NormalizeEntryName(texture.File), expectedPath, StringComparison.Ordinal))
                    throw new InvalidDataException("Invalid texture file path for DID " + did + ".");

                texture.Did = did;
                texture.File = expectedPath;
            }
        }

        private static void ValidateArchiveTextures(ZipArchive archive, AcuiManifest manifest)
        {
            foreach (AcuiTextureEntry texture in manifest.Textures)
            {
                ZipArchiveEntry entry = FindEntry(archive, texture.File);
                if (entry == null)
                    throw new InvalidDataException("The ACUI archive is missing " + texture.File + ".");
                if ((ulong)entry.Length != texture.ImageSize)
                    throw new InvalidDataException("Replacement byte count mismatch for DID " + texture.Did + ".");
            }
        }

        private static ZipArchiveEntry FindEntry(ZipArchive archive, string expectedName)
        {
            string normalizedExpected = NormalizeEntryName(expectedName);
            foreach (ZipArchiveEntry entry in archive.Entries)
            {
                if (String.Equals(NormalizeEntryName(entry.FullName), normalizedExpected, StringComparison.Ordinal))
                    return entry;
            }
            return null;
        }

        private static string NormalizeEntryName(string name)
        {
            return (name ?? String.Empty).Replace('\\', '/');
        }

        private static byte[] ReadEntryBytes(ZipArchiveEntry entry, int maxBytes)
        {
            if (entry.Length < 0 || entry.Length > maxBytes)
                throw new InvalidDataException(entry.FullName + " is unexpectedly large.");

            using (Stream input = entry.Open())
            using (MemoryStream output = new MemoryStream())
            {
                input.CopyTo(output);
                return output.ToArray();
            }
        }

        private static uint ParseHexUInt(string text, string field)
        {
            string value = text ?? String.Empty;
            if (value.StartsWith("0x", StringComparison.OrdinalIgnoreCase))
                value = value.Substring(2);

            uint parsed;
            if (!UInt32.TryParse(value, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out parsed))
                throw new InvalidDataException("Invalid " + field + ".");
            return parsed;
        }

        private static string BuildInstallId(string name, string author)
        {
            string safe = SanitizeFileName(name);
            string identity = (name ?? String.Empty) + "\n" + (author ?? String.Empty);
            byte[] hash;
            using (SHA256 sha = SHA256.Create())
                hash = sha.ComputeHash(Encoding.UTF8.GetBytes(identity));

            StringBuilder suffix = new StringBuilder(12);
            for (int i = 0; i < 6; ++i)
                suffix.Append(hash[i].ToString("x2", CultureInfo.InvariantCulture));

            return safe + "-" + suffix.ToString();
        }

        private static string SanitizeFileName(string name)
        {
            HashSet<char> invalid = new HashSet<char>(Path.GetInvalidFileNameChars());
            StringBuilder output = new StringBuilder();
            foreach (char c in (name ?? String.Empty).Trim())
            {
                if (invalid.Contains(c) || Char.IsControl(c))
                    output.Append('_');
                else
                    output.Append(c);
                if (output.Length >= 48)
                    break;
            }

            string result = output.ToString().Trim().TrimEnd('.');
            return result.Length == 0 ? "UI-Pack" : result;
        }
    }
}
