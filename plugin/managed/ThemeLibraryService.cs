// AC Customs official theme library: GitHub directory discovery, content-based
// change detection, verified PNG installation, and optional website submissions.
// All HTTP/file operations must run on worker threads, never VVS callbacks.
using System;
using System.Collections.Generic;
using System.Drawing;
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
    internal sealed class GithubThemeItem
    {
        [DataMember(Name = "name")] public string Name { get; set; }
        [DataMember(Name = "path")] public string Path { get; set; }
        [DataMember(Name = "sha")] public string Sha { get; set; }
        [DataMember(Name = "size")] public long Size { get; set; }
        [DataMember(Name = "type")] public string Type { get; set; }
    }

    [DataContract]
    internal sealed class ThemeSubmitResponse
    {
        [DataMember(Name = "id")] public string Id { get; set; }
        [DataMember(Name = "status")] public string Status { get; set; }
    }

    internal static class ThemeLibraryService
    {
        private const string GithubContentsUrl =
            "https://api.github.com/repos/nomadplayllc/AC-Customs/contents/themes?ref=main";
        private const string GithubRawRoot =
            "https://raw.githubusercontent.com/nomadplayllc/AC-Customs/main/themes/";
        // The site must implement POST /api/themes/submit before submissions work.
        // Never put GitHub write credentials in this client.
        private const string SubmitUrl =
            "https://ac-customs.mrgrid.chatgpt.site/api/themes/submit";
        internal const int MaxPngBytes = 12 * 1024 * 1024;
        private static readonly Regex SafeName = new Regex(
            @"\A[A-Za-z0-9][A-Za-z0-9 _.-]{0,79}\.png\z",
            RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
        private static readonly Regex GitSha = new Regex(
            @"\A[a-fA-F0-9]{40}\z", RegexOptions.CultureInvariant);
        internal static readonly string InstalledFolder = System.IO.Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ACCustoms", "Themes", "Official");

        internal static List<GithubThemeItem> GetAvailableThemes()
        {
            byte[] data;
            try { data = Download(GithubContentsUrl, 256 * 1024, true); }
            catch (WebException ex)
            {
                // GitHub returns 404 until the first themes/ PNG is committed.
                // Treat a not-yet-created directory as an empty library.
                HttpWebResponse response = ex.Response as HttpWebResponse;
                if (response != null && response.StatusCode == HttpStatusCode.NotFound)
                    return new List<GithubThemeItem>();
                throw;
            }
            List<GithubThemeItem> items;
            using (var stream = new MemoryStream(data, false))
                items = (List<GithubThemeItem>)new DataContractJsonSerializer(
                    typeof(List<GithubThemeItem>)).ReadObject(stream);
            var result = new List<GithubThemeItem>();
            var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            if (items == null) return result;
            foreach (GithubThemeItem item in items)
            {
                if (item == null || item.Type != "file" ||
                    !IsSafeName(item.Name) || !GitSha.IsMatch(item.Sha ?? "") ||
                    item.Size <= 0 || item.Size > MaxPngBytes ||
                    !String.Equals(item.Path, "themes/" + item.Name, StringComparison.Ordinal) ||
                    !names.Add(item.Name)) continue;
                result.Add(item);
            }
            result.Sort(delegate(GithubThemeItem a, GithubThemeItem b)
                { return StringComparer.OrdinalIgnoreCase.Compare(a.Name, b.Name); });
            return result;
        }

        internal static bool IsSafeName(string name)
        {
            return !String.IsNullOrWhiteSpace(name) && SafeName.IsMatch(name) &&
                   name.IndexOf("..", StringComparison.Ordinal) < 0 &&
                   !name.EndsWith(".png ", StringComparison.OrdinalIgnoreCase);
        }

        internal static string ThemePath(GithubThemeItem item)
        {
            if (item == null || !IsSafeName(item.Name))
                throw new InvalidDataException("Invalid theme filename.");
            return System.IO.Path.Combine(InstalledFolder, item.Name);
        }

        private static string MetadataPath(GithubThemeItem item)
        {
            return ThemePath(item) + ".githubsha";
        }

        // No extra catalog: the GitHub blob SHA is a content ID. We store it
        // alongside the official PNG after a verified install.
        internal static string InstalledSha(GithubThemeItem item)
        {
            try
            {
                string file = ThemePath(item);
                string sidecar = MetadataPath(item);
                if (!File.Exists(file) || !File.Exists(sidecar)) return null;
                string sha = File.ReadAllText(sidecar).Trim();
                return GitSha.IsMatch(sha) ? sha : null;
            }
            catch { return null; }
        }

        internal static string State(GithubThemeItem item)
        {
            string installed = InstalledSha(item);
            if (installed == null) return "Not installed";
            return String.Equals(installed, item.Sha, StringComparison.OrdinalIgnoreCase)
                ? "Installed" : "Update available";
        }

        internal static string GitBlobSha(byte[] content)
        {
            byte[] prefix = Encoding.ASCII.GetBytes("blob " + content.Length + "\0");
            using (SHA1 sha1 = SHA1.Create())
            {
                sha1.TransformBlock(prefix, 0, prefix.Length, prefix, 0);
                sha1.TransformFinalBlock(content, 0, content.Length);
                var result = new StringBuilder(40);
                foreach (byte b in sha1.Hash) result.Append(b.ToString("x2"));
                return result.ToString();
            }
        }

        internal static void ValidatePng(string path)
        {
            var file = new FileInfo(path);
            if (!file.Exists || file.Length < 33 || file.Length > MaxPngBytes)
                throw new InvalidDataException("Select a PNG between 33 bytes and 12 MB.");
            // Fully decode to reject renamed/non-PNG and corrupt images.
            using (var stream = File.OpenRead(path))
            using (Image image = Image.FromStream(stream, false, true))
            {
                if (image.RawFormat.Guid != System.Drawing.Imaging.ImageFormat.Png.Guid ||
                    image.Width != 2000 || image.Height != 2000)
                    throw new InvalidDataException("Theme must be a 2000 x 2000 PNG.");
            }
        }

        internal static string Install(GithubThemeItem item)
        {
            if (item == null || !IsSafeName(item.Name) || !GitSha.IsMatch(item.Sha ?? ""))
                throw new InvalidDataException("Invalid GitHub theme information.");
            byte[] downloaded = Download(GithubRawRoot + Uri.EscapeDataString(item.Name),
                MaxPngBytes, false);
            if (downloaded.Length != item.Size ||
                !String.Equals(GitBlobSha(downloaded), item.Sha,
                    StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("GitHub theme changed during download. Refresh and retry.");

            Directory.CreateDirectory(InstalledFolder);
            string target = ThemePath(item);
            string metadata = MetadataPath(item);
            string staged = target + ".incoming-" + Guid.NewGuid().ToString("N");
            try
            {
                File.WriteAllBytes(staged, downloaded);
                ValidatePng(staged);
                if (File.Exists(target))
                {
                    string previousSha = InstalledSha(item);
                    if (previousSha == null)
                        throw new InvalidDataException("An untracked local theme has the same name. Move it before installing.");
                    // Protect custom edits to an official download.
                    byte[] existing = File.ReadAllBytes(target);
                    if (!String.Equals(GitBlobSha(existing), previousSha,
                        StringComparison.OrdinalIgnoreCase))
                        throw new InvalidDataException("Local theme was edited. Save a custom copy before updating.");
                    if (String.Equals(previousSha, item.Sha, StringComparison.OrdinalIgnoreCase))
                        return target;
                    string backup = target + ".backup-" + DateTime.UtcNow.ToString("yyyyMMddHHmmss") +
                        "-" + Guid.NewGuid().ToString("N").Substring(0, 6) + ".png";
                    File.Replace(staged, target, backup);
                }
                else File.Move(staged, target);
                File.WriteAllText(metadata, item.Sha.ToLowerInvariant());
                return target;
            }
            finally { try { if (File.Exists(staged)) File.Delete(staged); } catch { } }
        }

        internal static string SubmitTheme(string sourcePng)
        {
            ValidatePng(sourcePng);
            string filename = System.IO.Path.GetFileName(sourcePng);
            if (!IsSafeName(filename))
                throw new InvalidDataException("Rename the PNG using letters, numbers, spaces or hyphens.");
            string boundary = "----ACCustoms-" + Guid.NewGuid().ToString("N");
            byte[] first = Encoding.UTF8.GetBytes("--" + boundary + "\r\n" +
                "Content-Disposition: form-data; name=\"theme\"; filename=\"" + filename + "\"\r\n" +
                "Content-Type: image/png\r\n\r\n");
            byte[] last = Encoding.UTF8.GetBytes("\r\n--" + boundary + "--\r\n");
            long size = new FileInfo(sourcePng).Length;
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            var request = (HttpWebRequest)WebRequest.Create(SubmitUrl);
            request.Method = "POST";
            request.UserAgent = "ACCustoms/1.3 theme-submission";
            request.ContentType = "multipart/form-data; boundary=" + boundary;
            request.ContentLength = first.Length + size + last.Length;
            request.Timeout = 30000;
            request.ReadWriteTimeout = 30000;
            request.AllowAutoRedirect = false; // Never transmit PNG to a redirect target.
            try
            {
                using (Stream target = request.GetRequestStream())
                using (FileStream png = File.OpenRead(sourcePng))
                {
                    target.Write(first, 0, first.Length);
                    png.CopyTo(target);
                    target.Write(last, 0, last.Length);
                }
                using (var response = (HttpWebResponse)request.GetResponse())
                {
                    if (response.StatusCode != HttpStatusCode.OK &&
                        response.StatusCode != HttpStatusCode.Accepted &&
                        response.StatusCode != HttpStatusCode.Created)
                        throw new IOException("Upload was not accepted (HTTP " +
                            (int)response.StatusCode + ").");
                    if (response.ContentType == null ||
                        response.ContentType.IndexOf("application/json",
                            StringComparison.OrdinalIgnoreCase) < 0)
                        throw new IOException("Upload endpoint returned a page, not a submission receipt.");
                    ThemeSubmitResponse receipt;
                    using (Stream stream = response.GetResponseStream())
                        receipt = (ThemeSubmitResponse)new DataContractJsonSerializer(
                            typeof(ThemeSubmitResponse)).ReadObject(stream);
                    if (receipt == null || String.IsNullOrWhiteSpace(receipt.Id) ||
                        !String.Equals(receipt.Status, "pending", StringComparison.OrdinalIgnoreCase))
                        throw new IOException("Upload response did not confirm pending review.");
                    return receipt.Id;
                }
            }
            catch (WebException ex)
            {
                var response = ex.Response as HttpWebResponse;
                if (response != null && (response.StatusCode == HttpStatusCode.NotFound ||
                    response.StatusCode == HttpStatusCode.MethodNotAllowed))
                    throw new IOException("Website submission endpoint is not configured yet.", ex);
                throw new IOException("Website upload failed: " + ex.Message, ex);
            }
        }

        private static byte[] Download(string url, int maxBytes, bool githubApi)
        {
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            var request = (HttpWebRequest)WebRequest.Create(url);
            request.Method = "GET";
            request.UserAgent = "ACCustoms/1.3 theme-library";
            request.Timeout = 12000;
            request.ReadWriteTimeout = 12000;
            request.AutomaticDecompression = DecompressionMethods.GZip | DecompressionMethods.Deflate;
            request.CachePolicy = new System.Net.Cache.RequestCachePolicy(
                System.Net.Cache.RequestCacheLevel.NoCacheNoStore);
            if (githubApi) request.Accept = "application/vnd.github+json";
            using (var response = (HttpWebResponse)request.GetResponse())
            using (Stream stream = response.GetResponseStream())
            using (var buffer = new MemoryStream())
            {
                if (response.ContentLength > maxBytes)
                    throw new InvalidDataException("Theme data is too large.");
                byte[] chunk = new byte[8192];
                int n;
                while ((n = stream.Read(chunk, 0, chunk.Length)) > 0)
                {
                    if (buffer.Length + n > maxBytes)
                        throw new InvalidDataException("Theme data exceeds size limit.");
                    buffer.Write(chunk, 0, n);
                }
                return buffer.ToArray();
            }
        }
    }
}
