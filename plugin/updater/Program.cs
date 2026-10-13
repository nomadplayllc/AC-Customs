// Standalone updater embedded into ACCustoms.dll; runs only after user clicks Update.
// This executable MUST NOT reference the AC Customs managed or native DLLs.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Threading;

namespace ACCustoms.Updater
{
    internal static class Program
    {
        private static readonly string LogPath = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ACCustoms", "plugin-updater.log");
        private static void Log(string message)
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(LogPath));
                File.AppendAllText(LogPath, DateTime.Now.ToString("u") + " " + message + Environment.NewLine);
            }
            catch { }
        }
        private static string Hash(string path)
        {
            using (var file = File.OpenRead(path))
            using (var sha = SHA256.Create())
                return BitConverter.ToString(sha.ComputeHash(file)).Replace("-", "");
        }
        private static bool Matches(string path, string hash)
        {
            return File.Exists(path) && String.Equals(Hash(path), hash, StringComparison.OrdinalIgnoreCase);
        }
        private static Dictionary<string, string> ReadArguments(string[] args)
        {
            var options = new Dictionary<string, string>(StringComparer.Ordinal);
            if (args.Length != 12) throw new ArgumentException("Updater command line is incomplete.");
            for (int i = 0; i < args.Length; i += 2)
            {
                if (!args[i].StartsWith("--", StringComparison.Ordinal) || options.ContainsKey(args[i]))
                    throw new ArgumentException("Invalid updater arguments.");
                options.Add(args[i], args[i + 1]);
            }
            return options;
        }
        private static string Required(Dictionary<string, string> d, string key)
        {
            string value;
            if (!d.TryGetValue(key, out value) || String.IsNullOrWhiteSpace(value))
                throw new ArgumentException("Missing argument " + key);
            return value;
        }
        private static bool ValidHash(string value)
        {
            if (value.Length != 64) return false;
            foreach (char c in value)
                if (!Uri.IsHexDigit(c)) return false;
            return true;
        }
        private static void WaitForExit(int pid, long startedTicks)
        {
            try
            {
                using (var source = Process.GetProcessById(pid))
                {
                    // A reused PID must not delay installation for an unrelated process.
                    if (source.StartTime.ToUniversalTime().Ticks != startedTicks) return;
                    source.WaitForExit();
                }
            }
            catch (ArgumentException) { /* Already exited. */ }
            catch (InvalidOperationException) { /* Already exited. */ }
        }
        private static void ReplaceOne(string folder, string staged, string name,
            string checksum, Dictionary<string, string> backups)
        {
            string dest = Path.Combine(folder, name);
            if (!File.Exists(dest))
                throw new FileNotFoundException("Expected installed plugin DLL is missing", dest);
            if (Matches(dest, checksum)) return;
            string backup = dest + ".preupdate-" + Guid.NewGuid().ToString("N") + ".bak";
            string prepared = dest + ".ac-update.new";
            if (File.Exists(prepared)) File.Delete(prepared);
            File.Copy(Path.Combine(staged, name), prepared);
            if (!Matches(prepared, checksum))
                throw new InvalidDataException("Prepared replacement checksum mismatch: " + name);
            try
            {
                // ReplaceFile is atomic per file on NTFS. The original DLL
                // is backed up so both can be rolled back on partial failure.
                File.Replace(prepared, dest, backup, true);
                backups[name] = backup;
            }
            finally { try { if (File.Exists(prepared)) File.Delete(prepared); } catch { } }
        }
        private static void Restore(string folder, Dictionary<string, string> backups)
        {
            foreach (string name in new[] { "ACCustoms.Native.dll", "ACCustoms.dll" })
            {
                string backup;
                if (!backups.TryGetValue(name, out backup) || !File.Exists(backup)) continue;
                string original = Path.Combine(folder, name);
                try
                {
                    // The old DLL is restored atomically when possible.
                    File.Replace(backup, original, null, true);
                    Log("Restored previous " + name);
                }
                catch (Exception ex) { Log("ROLLBACK FAILED for " + name + ": " + ex); }
            }
        }
        private static bool Install(string install, string staged, string managedHash, string nativeHash)
        {
            // Validate immediately before ANY writes to the installation folder.
            string managed = Path.Combine(staged, "ACCustoms.dll");
            string native = Path.Combine(staged, "ACCustoms.Native.dll");
            if (!Matches(managed, managedHash) || !Matches(native, nativeHash))
                throw new InvalidDataException("Downloaded DLL validation failed; installation cancelled.");
            var backups = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            try
            {
                ReplaceOne(install, staged, "ACCustoms.Native.dll", nativeHash, backups);
                ReplaceOne(install, staged, "ACCustoms.dll", managedHash, backups);
                if (!Matches(Path.Combine(install, "ACCustoms.dll"), managedHash) ||
                    !Matches(Path.Combine(install, "ACCustoms.Native.dll"), nativeHash))
                    throw new IOException("Installed DLL checksum verification failed.");
                Log("Installed and verified both AC Customs DLLs into " + install);
                foreach (string backup in backups.Values)
                    try { File.Delete(backup); } catch { }
                try { File.WriteAllText(Path.Combine(staged, "installation-result.txt"),
                    "Installed successfully: " + DateTime.Now.ToString("u")); }
                catch { /* A successful install must not be undone by status write failure. */ }
                return true;
            }
            catch
            {
                Restore(install, backups);
                throw;
            }
        }
        private static bool SharingViolation(IOException ex)
        {
            uint code = unchecked((uint)ex.HResult);
            return code == 0x80070020 || code == 0x80070021;
        }
        private static int Main(string[] args)
        {
            try
            {
                var options = ReadArguments(args);
                int pid = Int32.Parse(Required(options, "--pid"));
                long ticks = Int64.Parse(Required(options, "--start-ticks"));
                string install = Path.GetFullPath(Required(options, "--install"));
                string staged = Path.GetFullPath(Required(options, "--staged"));
                string managedHash = Required(options, "--managed-hash");
                string nativeHash = Required(options, "--native-hash");
                if (!ValidHash(managedHash) || !ValidHash(nativeHash))
                    throw new ArgumentException("Invalid DLL SHA-256 hash.");
                if (!Directory.Exists(install) || !Directory.Exists(staged))
                    throw new DirectoryNotFoundException("Update directories do not exist.");
                if (!Matches(Path.Combine(staged, "ACCustoms.dll"), managedHash) ||
                    !Matches(Path.Combine(staged, "ACCustoms.Native.dll"), nativeHash))
                    throw new InvalidDataException("Staged plugin update is invalid.");

                string lockName;
                using (var sha = SHA256.Create())
                    lockName = BitConverter.ToString(sha.ComputeHash(
                        Encoding.UTF8.GetBytes(install.ToLowerInvariant()))).Replace("-", "");
                using (var mutex = new Mutex(false, "Local\\ACCustomsUpdater-" + lockName))
                {
                    bool acquired;
                    try { acquired = mutex.WaitOne(0); }
                    catch (AbandonedMutexException) { acquired = true; }
                    if (!acquired)
                    {
                        Log("Another updater is already waiting for this installation.");
                        return 0;
                    }
                    try
                    {
                        Log("Update downloaded. Waiting for client PID " + pid + " to exit.");
                        WaitForExit(pid, ticks);
                        // Multiple AC clients may still have the native DLL loaded.
                        // Do not overwrite any loaded DLL; keep retrying after exits.
                        Exception last = null;
                        for (int attempt = 0; attempt < 18000; attempt++) // up to 10 hours after exit
                        {
                            try { return Install(install, staged, managedHash, nativeHash) ? 0 : 1; }
                            catch (IOException ex) when (SharingViolation(ex)) { last = ex; }
                            Thread.Sleep(2000);
                        }
                        throw new IOException("Could not install update after game exit.", last);
                    }
                    finally { mutex.ReleaseMutex(); }
                }
            }
            catch (Exception ex)
            {
                Log("UPDATE FAILED: " + ex);
                return 1;
            }
        }
    }
}
