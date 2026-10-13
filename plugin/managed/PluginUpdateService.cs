// Managed-side plugin update checker and verified downloader.
// Never writes into Decal's installation folder while AC is running.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Reflection;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;

namespace ACCustoms
{
    [DataContract]
    internal sealed class PluginRelease
    {
        [DataMember(Name = "version")] public string Version { get; set; }
        [DataMember(Name = "changelog")] public List<string> Changelog { get; set; }
        [DataMember(Name = "files")] public Dictionary<string, string> Files { get; set; }
        [DataMember(Name = "sha256")] public Dictionary<string, string> Sha256 { get; set; }
        [DataMember(Name = "bytes")] public Dictionary<string, long> Bytes { get; set; }
    }

    internal static class PluginUpdateService
    {
        internal const string ManagedName = "ACCustoms.dll";
        internal const string NativeName = "ACCustoms.Native.dll";
        private const string RawRoot =
            "https://raw.githubusercontent.com/nomadplayllc/AC-Customs/main/updates/";
        private const int ManifestLimit = 65536;
        private const int FileLimit = 32 * 1024 * 1024;
        private static readonly Regex VersionPattern = new Regex(
            @"\A[0-9]+\.[0-9]+\.[0-9]+(?:\.[0-9]+)?\z", RegexOptions.CultureInvariant);
        private static readonly Regex HashPattern = new Regex(
            @"\A[a-fA-F0-9]{64}\z", RegexOptions.CultureInvariant);
        private static readonly string Root = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ACCustoms", "PluginUpdates");
        internal static string InstalledVersion
        {
            get { return typeof(PluginCore).Assembly.GetName().Version.ToString(); }
        }
        internal static bool IsNewer(PluginRelease release)
        {
            Version available;
            return release != null && Version.TryParse(release.Version, out available) &&
                available.CompareTo(typeof(PluginCore).Assembly.GetName().Version) > 0;
        }
        internal static string StageFolder(PluginRelease release)
        {
            Validate(release);
            return Path.Combine(Root, "v" + release.Version);
        }
        internal static void Validate(PluginRelease release)
        {
            if (release == null || release.Version == null ||
                !VersionPattern.IsMatch(release.Version) ||
                release.Files == null || release.Sha256 == null || release.Bytes == null)
                throw new InvalidDataException("Invalid plugin release manifest.");
            Version ignored;
            if (!Version.TryParse(release.Version, out ignored))
                throw new InvalidDataException("Invalid plugin release version.");
            foreach (string name in new[] { ManagedName, NativeName })
            {
                string relative, checksum;
                long bytes;
                if (!release.Files.TryGetValue(name, out relative) ||
                    !String.Equals(relative, "releases/v" + release.Version + "/" + name,
                        StringComparison.Ordinal) ||
                    !release.Sha256.TryGetValue(name, out checksum) || !HashPattern.IsMatch(checksum) ||
                    !release.Bytes.TryGetValue(name, out bytes) || bytes <= 0 || bytes > FileLimit)
                    throw new InvalidDataException("Invalid plugin asset: " + name);
            }
            if (release.Changelog == null) release.Changelog = new List<string>();
            if (release.Changelog.Count > 16)
                release.Changelog = release.Changelog.GetRange(0, 16);
            for (int i = 0; i < release.Changelog.Count; i++)
            {
                string note = (release.Changelog[i] ?? "").Replace("\r", " ").Replace("\n", " ");
                release.Changelog[i] = note.Length > 320 ? note.Substring(0, 320) : note;
            }
        }
        private static byte[] GetBytes(string url, int maxBytes)
        {
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            var request = (HttpWebRequest)WebRequest.Create(url);
            request.UserAgent = "ACCustoms-PluginUpdater/1.0";
            request.Timeout = 12000;
            request.ReadWriteTimeout = 12000;
            request.CachePolicy = new System.Net.Cache.RequestCachePolicy(
                System.Net.Cache.RequestCacheLevel.NoCacheNoStore);
            using (var response = (HttpWebResponse)request.GetResponse())
            using (var input = response.GetResponseStream())
            using (var output = new MemoryStream())
            {
                if (response.StatusCode != HttpStatusCode.OK || response.ContentLength > maxBytes)
                    throw new IOException("Unexpected update response from GitHub.");
                byte[] buffer = new byte[16384];
                int count;
                while ((count = input.Read(buffer, 0, buffer.Length)) != 0)
                {
                    if (output.Length + count > maxBytes)
                        throw new IOException("Plugin download exceeded size limit.");
                    output.Write(buffer, 0, count);
                }
                return output.ToArray();
            }
        }
        internal static PluginRelease GetLatest()
        {
            // GitHub raw URLs can fail briefly at game login. Never block startup.
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                try
                {
                    byte[] json = GetBytes(RawRoot + "latest.json", ManifestLimit);
                    PluginRelease release;
                    using (var stream = new MemoryStream(json))
                        release = (PluginRelease)new DataContractJsonSerializer(typeof(PluginRelease),
                            new DataContractJsonSerializerSettings { UseSimpleDictionaryFormat = true })
                            .ReadObject(stream);
                    Validate(release);
                    return release;
                }
                catch (WebException) when (attempt < 2)
                {
                    Thread.Sleep((attempt + 1) * 750);
                }
            }
            throw new IOException("Unable to retrieve plugin update manifest.");
        }
        internal static string Sha256(string path)
        {
            using (var file = File.OpenRead(path))
            using (var sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(file)).Replace("-", "");
        }
        internal static bool IsStaged(PluginRelease release)
        {
            try
            {
                string folder = StageFolder(release);
                foreach (string name in new[] { ManagedName, NativeName })
                {
                    string path = Path.Combine(folder, name);
                    if (!File.Exists(path) || new FileInfo(path).Length != release.Bytes[name] ||
                        !String.Equals(Sha256(path), release.Sha256[name],
                            StringComparison.OrdinalIgnoreCase)) return false;
                }
                return true;
            }
            catch { return false; }
        }
        internal static string DownloadAndStage(PluginRelease release)
        {
            Validate(release);
            if (IsStaged(release)) return StageFolder(release);
            Directory.CreateDirectory(Root);
            string target = StageFolder(release);
            string temp = target + ".downloading-" + Guid.NewGuid().ToString("N");
            try
            {
                Directory.CreateDirectory(temp);
                foreach (string name in new[] { ManagedName, NativeName })
                {
                    byte[] bytes = GetBytes(RawRoot + release.Files[name], FileLimit);
                    if (bytes.LongLength != release.Bytes[name])
                        throw new InvalidDataException(name + " length mismatch.");
                    string path = Path.Combine(temp, name);
                    File.WriteAllBytes(path, bytes);
                    if (!String.Equals(Sha256(path), release.Sha256[name],
                            StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException(name + " checksum mismatch.");
                }
                // Do not allow partial downloads to look like completed updates.
                if (Directory.Exists(target)) Directory.Delete(target, true);
                Directory.Move(temp, target);
                return target;
            }
            finally { if (Directory.Exists(temp)) Directory.Delete(temp, true); }
        }
        internal static void ScheduleInstall(PluginRelease release)
        {
            if (!IsStaged(release)) throw new InvalidDataException("Staged update failed verification.");
            string location = typeof(PluginCore).Assembly.Location;
            string installDir = Path.GetDirectoryName(location);
            if (!String.Equals(Path.GetFileName(location), ManagedName, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("Cannot identify the loaded ACCustoms.dll path.");
            string helper = ExtractHelper();
            using (var process = Process.GetCurrentProcess())
            {
                string args = "--pid " + process.Id +
                    " --start-ticks " + process.StartTime.ToUniversalTime().Ticks +
                    " --install " + Quote(installDir) +
                    " --staged " + Quote(StageFolder(release)) +
                    " --managed-hash " + release.Sha256[ManagedName] +
                    " --native-hash " + release.Sha256[NativeName];
                // Protected installation folders may require elevation. Prompt
                // for UAC now (on the user's Update click), not after AC exits.
                bool elevated = !CanWriteDirectory(installDir);
                var info = new ProcessStartInfo(helper, args) {
                    UseShellExecute = elevated, CreateNoWindow = !elevated,
                    WorkingDirectory = Path.GetDirectoryName(helper)
                };
                if (elevated) info.Verb = "runas";
                using (var updater = Process.Start(info))
                    if (updater == null) throw new IOException("Updater helper did not start.");
            }
        }
        private static bool CanWriteDirectory(string folder)
        {
            string probe = Path.Combine(folder, ".ac-customs-permissions-" +
                Guid.NewGuid().ToString("N") + ".tmp");
            try
            {
                using (var file = new FileStream(probe, FileMode.CreateNew,
                    FileAccess.Write, FileShare.None)) { }
                return true;
            }
            catch (UnauthorizedAccessException) { return false; }
            catch (IOException) { return false; }
            finally { try { if (File.Exists(probe)) File.Delete(probe); } catch { } }
        }
        private static string Quote(string value)
        {
            // Windows ProcessStartInfo.Arguments: handle trailing backslashes and quotes.
            var quoted = new StringBuilder("\"");
            int slashes = 0;
            foreach (char c in value)
            {
                if (c == '\\') { slashes++; continue; }
                if (c == '\"')
                {
                    quoted.Append('\\', slashes * 2 + 1);
                    quoted.Append('\"');
                    slashes = 0;
                    continue;
                }
                quoted.Append('\\', slashes);
                slashes = 0;
                quoted.Append(c);
            }
            quoted.Append('\\', slashes * 2);
            return quoted.Append('\"').ToString();
        }
        private static string ExtractHelper()
        {
            const string resource = "ACCustoms.Updater.exe";
            byte[] bytes;
            using (Stream input = typeof(PluginCore).Assembly.GetManifestResourceStream(resource))
            {
                if (input == null) throw new InvalidOperationException("Embedded updater helper missing.");
                using (var output = new MemoryStream())
                {
                    input.CopyTo(output);
                    bytes = output.ToArray();
                }
            }
            string hash;
            using (var sha = SHA256.Create())
                hash = BitConverter.ToString(sha.ComputeHash(bytes)).Replace("-", "");
            string folder = Path.Combine(Root, "Helper");
            Directory.CreateDirectory(folder);
            string path = Path.Combine(folder, "ACCustoms.Updater-" + hash.Substring(0, 16) + ".exe");
            if (File.Exists(path) && String.Equals(Sha256(path), hash, StringComparison.OrdinalIgnoreCase))
                return path;
            string temp = path + ".tmp-" + Guid.NewGuid().ToString("N");
            try
            {
                File.WriteAllBytes(temp, bytes);
                if (File.Exists(path)) File.Delete(path);
                File.Move(temp, path);
            }
            finally { if (File.Exists(temp)) File.Delete(temp); }
            if (!String.Equals(Sha256(path), hash, StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Embedded update helper verification failed.");
            return path;
        }
    }
}
