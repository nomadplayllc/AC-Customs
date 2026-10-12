// Downloads the sole official template PNG from the public GitHub template/ directory.
// Does not affect installed themes, mappings, or the active UI.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Net;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Text.RegularExpressions;

namespace ACCustoms
{
    [DataContract]
    internal sealed class OfficialTemplateGithubFile
    {
        [DataMember(Name = "name")] public string Name { get; set; }
        [DataMember(Name = "sha")] public string Sha { get; set; }
        [DataMember(Name = "size")] public long Size { get; set; }
        [DataMember(Name = "type")] public string Type { get; set; }
        [DataMember(Name = "path")] public string Path { get; set; }
    }

    internal static class TemplateDownloadService
    {
        private const string Api = "https://api.github.com/repos/nomadplayllc/AC-Customs/contents/template?ref=main";
        private const string Raw = "https://raw.githubusercontent.com/nomadplayllc/AC-Customs/main/template/";
        private const int MaxBytes = 12 * 1024 * 1024;
        private static readonly Regex ValidName = new Regex(@"\A[A-Za-z0-9][A-Za-z0-9 _.-]{0,79}\.png\z",
            RegexOptions.IgnoreCase | RegexOptions.CultureInvariant);
        private static readonly Regex ValidGitSha = new Regex(@"\A[a-fA-F0-9]{40}\z", RegexOptions.CultureInvariant);

        internal static string DownloadLatest()
        {
            byte[] listing;
            try { listing = ReadUrl(Api, 256 * 1024); }
            catch (WebException ex)
            {
                var response = ex.Response as HttpWebResponse;
                if (response != null && response.StatusCode == HttpStatusCode.NotFound)
                    throw new IOException("GitHub template/ folder not found yet.", ex);
                throw;
            }
            List<OfficialTemplateGithubFile> files;
            using (var stream = new MemoryStream(listing, false))
                files = (List<OfficialTemplateGithubFile>)new DataContractJsonSerializer(
                    typeof(List<OfficialTemplateGithubFile>)).ReadObject(stream);
            OfficialTemplateGithubFile chosen = null;
            if (files != null)
                foreach (var file in files)
                {
                    if (file == null || file.Type != "file" || file.Name == null ||
                        !file.Name.EndsWith(".png", StringComparison.OrdinalIgnoreCase)) continue;
                    if (chosen != null)
                        throw new InvalidDataException("template/ must contain exactly one PNG.");
                    chosen = file;
                }
            if (chosen == null)
                throw new FileNotFoundException("No PNG in GitHub template/ folder.");
            if (!ValidName.IsMatch(chosen.Name) || chosen.Name.Contains("..") ||
                !String.Equals(chosen.Path, "template/" + chosen.Name, StringComparison.Ordinal) ||
                !ValidGitSha.IsMatch(chosen.Sha ?? "") ||
                chosen.Size <= 0 || chosen.Size > MaxBytes)
                throw new InvalidDataException("The official template filename or metadata is invalid.");

            byte[] png = ReadUrl(Raw + Uri.EscapeDataString(chosen.Name), MaxBytes);
            if (png.LongLength != chosen.Size ||
                !String.Equals(ThemeLibraryService.GitBlobSha(png), chosen.Sha,
                    StringComparison.OrdinalIgnoreCase))
                throw new IOException("Template changed during download; try again.");
            using (var stream = new MemoryStream(png, false))
            using (var image = Image.FromStream(stream, false, true))
            {
                if (image.RawFormat.Guid != System.Drawing.Imaging.ImageFormat.Png.Guid ||
                    image.Width != 2000 || image.Height != 2000)
                    throw new InvalidDataException("Official template must be a 2000 x 2000 PNG.");
            }
            string downloads = System.IO.Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads");
            if (!Directory.Exists(downloads))
                downloads = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
            if (!Directory.Exists(downloads))
                throw new DirectoryNotFoundException("No Downloads or Documents folder found.");
            string name = System.IO.Path.GetFileNameWithoutExtension(chosen.Name);
            string destination = System.IO.Path.Combine(downloads, chosen.Name);
            for (int index = 1; File.Exists(destination); index++)
            {
                if (index > 10000) throw new IOException("Too many existing template downloads.");
                destination = System.IO.Path.Combine(downloads, name + " (" + index + ").png");
            }
            string staging = System.IO.Path.Combine(downloads,
                ".accustoms-template-" + Guid.NewGuid().ToString("N") + ".tmp");
            try
            {
                File.WriteAllBytes(staging, png);
                File.Move(staging, destination);
                return destination;
            }
            finally
            {
                if (File.Exists(staging)) try { File.Delete(staging); } catch { }
            }
        }

        private static byte[] ReadUrl(string url, int maxBytes)
        {
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            var request = (HttpWebRequest)WebRequest.Create(url);
            request.UserAgent = "ACCustoms/1.3 (Theme Template Downloader)";
            request.Accept = "application/vnd.github+json, application/octet-stream";
            request.Timeout = 15000;
            request.ReadWriteTimeout = 15000;
            request.AutomaticDecompression = DecompressionMethods.GZip | DecompressionMethods.Deflate;
            using (var response = (HttpWebResponse)request.GetResponse())
            using (var stream = response.GetResponseStream())
            using (var output = new MemoryStream())
            {
                byte[] buffer = new byte[8192];
                int n;
                while ((n = stream.Read(buffer, 0, buffer.Length)) > 0)
                {
                    if (output.Length + n > maxBytes)
                        throw new InvalidDataException("GitHub file exceeds size limit.");
                    output.Write(buffer, 0, n);
                }
                return output.ToArray();
            }
        }
    }
}
