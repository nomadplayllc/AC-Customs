// AC Customs - opt-in, manifest-driven mapping release checks.
// All network and disk IO happens on ThreadPool workers, never in VVS callbacks.
// A download only stages versioned source files; it never alters a user's
// selected mapping, edited PNG, or compiled texture outputs.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace ACCustoms
{
    [DataContract]
    internal sealed class MappingRelease
    {
        [DataMember(Name = "mappingVersion")] public string MappingVersion { get; set; }
        [DataMember(Name = "minimumPluginVersion")] public string MinimumPluginVersion { get; set; }
        [DataMember(Name = "changelog")] public List<string> Changelog { get; set; }
        [DataMember(Name = "files")] public Dictionary<string, string> Files { get; set; }
        [DataMember(Name = "sha256")] public Dictionary<string, string> Sha256 { get; set; }
        [DataMember(Name = "bytes")] public Dictionary<string, long> Bytes { get; set; }
    }

    internal static class MappingUpdateService
    {
        // Publish mappings/latest.json on the public main branch. Do not accept
        // arbitrary hosts, full URLs, '..', or rooted paths from the manifest.
        private const string RawRoot =
            "https://raw.githubusercontent.com/nomadplayllc/AC-Customs/main/mappings/";
        private const string ManifestUrl = RawRoot + "latest.json";
        private const int ManifestLimit = 65536;
        private const int FileLimit = 12 * 1024 * 1024;
        private static readonly Regex ValidRelativeFile = new Regex(
            @"^versions/v[0-9]+\.[0-9]+(?:\.[0-9]+)?/[A-Za-z0-9_.-]+$",
            RegexOptions.CultureInvariant);
        private static readonly Regex ValidHash = new Regex(
            @"\A[a-fA-F0-9]{64}\z", RegexOptions.CultureInvariant);

        internal static string TargetFolder(MappingRelease release)
        {
            string profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            return Path.Combine(profile, "Downloads", "ACCustoms", "v" + release.MappingVersion);
        }

        internal static MappingRelease GetLatest()
        {
            var release = Parse(DownloadBytes(ManifestUrl, ManifestLimit));
            Validate(release);
            return release;
        }

        internal static MappingRelease Parse(byte[] json)
        {
            using (var stream = new MemoryStream(json, false))
                return (MappingRelease)new DataContractJsonSerializer(typeof(MappingRelease),
                        new DataContractJsonSerializerSettings { UseSimpleDictionaryFormat = true })
                    .ReadObject(stream);
        }

        private static void Validate(MappingRelease release)
        {
            if (release == null || String.IsNullOrWhiteSpace(release.MappingVersion) ||
                !Regex.IsMatch(release.MappingVersion, @"\A[0-9]+\.[0-9]+(?:\.[0-9]+)?\z") ||
                release.Files == null || release.Sha256 == null)
                throw new InvalidDataException("Mapping release manifest is missing required fields.");

            Version parsed;
            if (!Version.TryParse(release.MappingVersion, out parsed))
                throw new InvalidDataException("Invalid mapping release version.");

            if (String.IsNullOrWhiteSpace(release.MinimumPluginVersion) ||
                !Version.TryParse(release.MinimumPluginVersion, out parsed))
                throw new InvalidDataException("Missing minimum plugin version.");

            if (release.Changelog == null) release.Changelog = new List<string>();
            if (release.Changelog.Count > 20) release.Changelog = release.Changelog.GetRange(0, 20);
            foreach (var name in new[] { "mapping", "template", "darkMode" })
            {
                if (name == "darkMode" && !release.Files.ContainsKey(name)) continue;
                string path, checksum;
                if (!release.Files.TryGetValue(name, out path) ||
                    !ValidRelativeFile.IsMatch(path) ||
                    !path.StartsWith("versions/v" + release.MappingVersion + "/",
                        StringComparison.Ordinal) ||
                    Path.GetFileName(path).StartsWith(".", StringComparison.Ordinal) ||
                    !path.EndsWith(name == "mapping" ? ".json" : ".png",
                        StringComparison.OrdinalIgnoreCase) ||
                    !release.Sha256.TryGetValue(name, out checksum) ||
                    !ValidHash.IsMatch(checksum))
                    throw new InvalidDataException("Manifest contains an invalid " + name + " asset.");

                long fileSize;
                if (release.Bytes != null && release.Bytes.TryGetValue(name, out fileSize) &&
                    (fileSize <= 0 || fileSize > FileLimit))
                    throw new InvalidDataException("Invalid size for " + name + ".");
            }
        }

        private static byte[] DownloadBytes(string url, int maxBytes)
        {
            var request = (HttpWebRequest)WebRequest.Create(url);
            request.Method = "GET";
            request.UserAgent = "ACCustoms-MappingUpdate/1.0";
            request.Timeout = 10000;
            request.ReadWriteTimeout = 10000;
            request.AutomaticDecompression = DecompressionMethods.GZip | DecompressionMethods.Deflate;
            using (var response = (HttpWebResponse)request.GetResponse())
            using (var stream = response.GetResponseStream())
            using (var output = new MemoryStream())
            {
                if (response.StatusCode != HttpStatusCode.OK ||
                    (response.ContentLength > maxBytes))
                    throw new IOException("Invalid response from GitHub.");
                byte[] buffer = new byte[16384];
                int read;
                while ((read = stream.Read(buffer, 0, buffer.Length)) != 0)
                {
                    if (output.Length + read > maxBytes)
                        throw new IOException("GitHub download exceeded the size limit.");
                    output.Write(buffer, 0, read);
                }
                return output.ToArray();
            }
        }

        internal static string FileHash(string path)
        {
            using (var input = File.OpenRead(path))
            using (var sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(input)).Replace("-", "");
        }

        // The release descriptor is saved beside downloaded files. A version
        // counts as installed only after the user chooses that exact mapping
        // in Advanced, and only while its hash still matches the descriptor.
        internal static string InstalledVersion(string selectedMapping, string bundledMapping,
            MappingRelease current)
        {
            try
            {
                if (String.IsNullOrWhiteSpace(selectedMapping) || !File.Exists(selectedMapping))
                    return "Unknown";
                string currentHash = FileHash(selectedMapping);
                string advertisedHash;
                if (current != null && current.Sha256 != null &&
                    current.Sha256.TryGetValue("mapping", out advertisedHash) &&
                    String.Equals(currentHash, advertisedHash, StringComparison.OrdinalIgnoreCase))
                    return current.MappingVersion;

                string sidecar = Path.Combine(Path.GetDirectoryName(selectedMapping), "release-info.json");
                if (File.Exists(sidecar))
                {
                    byte[] bytes = File.ReadAllBytes(sidecar);
                    if (bytes.Length > 0 && bytes.Length <= ManifestLimit)
                    {
                        var installed = Parse(bytes);
                        Validate(installed);
                        string storedHash;
                        if (installed.Sha256.TryGetValue("mapping", out storedHash) &&
                            String.Equals(currentHash, storedHash, StringComparison.OrdinalIgnoreCase))
                            return installed.MappingVersion;
                    }
                }
            }
            catch (Exception) { /* Legacy or hand-edited mappings remain usable. */ }
            return String.Equals(selectedMapping, bundledMapping, StringComparison.OrdinalIgnoreCase)
                ? "Bundled" : "Custom/Unknown";
        }

        internal static bool IsNewer(string available, string installed)
        {
            Version a, b;
            if (!Version.TryParse(available, out a)) return false;
            if (!Version.TryParse(installed, out b)) return true;
            return a.CompareTo(b) > 0;
        }

        internal static bool PluginSupported(MappingRelease release)
        {
            Version needed;
            if (!Version.TryParse(release.MinimumPluginVersion, out needed)) return false;
            return typeof(PluginCore).Assembly.GetName().Version.CompareTo(needed) >= 0;
        }

        internal static string FindVerifiedDownload(MappingRelease release)
        {
            if (release == null) return String.Empty;
            try
            {
                string folder = TargetFolder(release);
                if (!Directory.Exists(folder)) return String.Empty;
                foreach (string name in new[] { "mapping", "template", "darkMode" })
                {
                    string relative;
                    if (!release.Files.TryGetValue(name, out relative)) continue;
                    string filename = Path.Combine(folder, Path.GetFileName(relative));
                    if (!File.Exists(filename) ||
                        !String.Equals(FileHash(filename), release.Sha256[name],
                            StringComparison.OrdinalIgnoreCase))
                        return String.Empty;
                }
                return folder;
            }
            catch (Exception) { return String.Empty; }
        }

        internal static string DownloadRelease(MappingRelease release)
        {
            Validate(release);
            if (!PluginSupported(release))
                throw new InvalidOperationException("This mapping release requires a newer AC Customs plugin.");

            string final = TargetFolder(release);
            if (FindVerifiedDownload(release) == final) return final;
            Directory.CreateDirectory(Path.GetDirectoryName(final));
            string staged = final + ".downloading-" + Guid.NewGuid().ToString("N");
            try
            {
                Directory.CreateDirectory(staged);
                foreach (string name in new[] { "mapping", "template", "darkMode" })
                {
                    string relative;
                    if (!release.Files.TryGetValue(name, out relative)) continue;
                    byte[] bytes = DownloadBytes(RawRoot + relative, FileLimit);
                    long expectedSize;
                    if (release.Bytes != null && release.Bytes.TryGetValue(name, out expectedSize) &&
                        bytes.LongLength != expectedSize)
                        throw new IOException(name + " size does not match the release manifest.");
                    using (var sha = SHA256.Create())
                    {
                        string checksum = BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "");
                        if (!String.Equals(checksum, release.Sha256[name],
                                StringComparison.OrdinalIgnoreCase))
                            throw new IOException(name + " checksum mismatch. No files were installed.");
                    }
                    File.WriteAllBytes(Path.Combine(staged, Path.GetFileName(relative)), bytes);
                }

                using (var output = File.Create(Path.Combine(staged, "release-info.json")))
                    new DataContractJsonSerializer(typeof(MappingRelease),
                        new DataContractJsonSerializerSettings { UseSimpleDictionaryFormat = true }).WriteObject(output, release);
                File.WriteAllText(Path.Combine(staged, "HOW_TO_INSTALL.txt"),
                    "AC Customs official mapping release v" + release.MappingVersion + "\r\n\r\n" +
                    "1. Open AC Customs in-game.\r\n" +
                    "2. Click Advanced, then Change Mapping. Choose " +
                    Path.GetFileName(release.Files["mapping"]) + ".\r\n" +
                    "3. Optionally open " + Path.GetFileName(release.Files["template"]) +
                    " as the latest default template when creating/updating your art.\r\n" +
                    "4. Regenerate textures and click Apply UI.\r\n\r\n" +
                    "Existing custom PNGs are not modified automatically.\r\n");

                // Versioned destination only. Never write over the embedded
                // mapping, the user's custom mapping, or their theme PNG.
                // Existing downloaded files may have been edited by the user.
                // Never erase or replace them without permission.
                if (Directory.Exists(final))
                    throw new IOException("Release folder already exists. Rename it if you wish to download again.");
                Directory.Move(staged, final);
                return final;
            }
            finally
            {
                if (Directory.Exists(staged))
                {
                    try { Directory.Delete(staged, true); } catch { }
                }
            }
        }
    }
}
