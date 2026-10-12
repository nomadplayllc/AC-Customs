// Immutable, content-addressed caches for custom PNG themes.
// A new source PNG, mapping JSON, or compiler version creates a NEW directory.
// No generator is permitted to overwrite the active native replacement directory.
using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace ACCustoms
{
    internal static class ThemeCache
    {
        private static readonly string Root = Path.Combine(PackManager.RuntimeDirectory, "ThemeCache");
        private const string Prefix = "ThemeCache_";

        private static string Sha256File(string path)
        {
            using (var input = File.Open(path, FileMode.Open, FileAccess.Read, FileShare.Read))
            using (var hash = SHA256.Create())
                return BitConverter.ToString(hash.ComputeHash(input)).Replace("-", "").ToLowerInvariant();
        }

        // Include source path in the cache key because compiler receipts also pin
        // SourcePath; copying an identical PNG elsewhere should have its own receipt.
        internal static string CacheId(string pngPath, string mappingPath)
        {
            string text = Path.GetFullPath(pngPath).ToUpperInvariant() + "\n" +
                          Sha256File(pngPath) + "\n" + Sha256File(mappingPath) + "\n" +
                          TemplateTextureCompiler.Version;
            using (var sha = SHA256.Create())
                return Prefix + BitConverter.ToString(sha.ComputeHash(Encoding.UTF8.GetBytes(text)))
                    .Replace("-", "").ToLowerInvariant().Substring(0, 32);
        }

        internal static string DirectoryFor(string pngPath, string mappingPath)
        {
            return Path.Combine(Root, CacheId(pngPath, mappingPath));
        }

        internal static InstalledPack GetPack(string pngPath, string mappingPath)
        {
            string folder = DirectoryFor(pngPath, mappingPath);
            return new InstalledPack {
                InstallId = Path.GetFileName(folder),
                Name = Path.GetFileNameWithoutExtension(pngPath),
                Author = "AC Customs",
                RootDirectory = folder,
                TexturesDirectory = Path.Combine(folder, "textures")
            };
        }

        internal static bool IsCachePack(string id)
        {
            return !String.IsNullOrEmpty(id) && id.StartsWith(Prefix, StringComparison.Ordinal);
        }
    }
}
