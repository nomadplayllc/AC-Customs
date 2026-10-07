// Managed/native boundary for AC Customs.
// Keep exported function names in sync with plugin/native/ACCustoms.Native.cpp.
// The native DLL is loaded dynamically so Decal.Adapter remains the only managed
// runtime dependency supplied by Decal itself.

using System;
using System.ComponentModel;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace ACCustoms
{
    internal static class NativeBridge
    {
        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr LoadLibrary(string lpFileName);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Ansi)]
        private static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int NativeCall();

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int NativeSetFlag(int enabled);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private delegate int NativeSetDirectory([MarshalAs(UnmanagedType.LPStr)] string directory);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int NativeGetHoveredTexture(out uint did, out uint width, out uint height);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        private delegate int NativeGetHoveredTextureStack(StringBuilder buffer, uint bufferSize);

        private static readonly object Sync = new object();
        private static IntPtr module = IntPtr.Zero;
        private static NativeCall initialize;
        private static NativeCall applyTestReplacement;
        private static NativeCall restoreVanilla;
        private static NativeCall getActiveMode;
        private static NativeSetFlag setDeveloperTests;
        private static NativeSetDirectory setReplacementDirectory;
        private static NativeSetDirectory setCaptureDirectory;
        private static NativeSetDirectory captureUiSnapshot;
        private static NativeSetDirectory captureControlStateLinks;
        private static NativeGetHoveredTexture getHoveredTexture;
        private static NativeGetHoveredTextureStack getHoveredTextureStack;
        private static NativeCall shutdown;

        public static bool IsLoaded { get { return module != IntPtr.Zero; } }

        private static T Resolve<T>(string name) where T : class
        {
            IntPtr proc = GetProcAddress(module, name);
            if (proc == IntPtr.Zero)
                throw new EntryPointNotFoundException(name);

            return Marshal.GetDelegateForFunctionPointer(proc, typeof(T)) as T;
        }

        private static void EnsureLoaded()
        {
            if (module != IntPtr.Zero)
                return;

            string pluginDir = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
            string nativePath = Path.Combine(pluginDir, "ACCustoms.Native.dll");

            if (!File.Exists(nativePath))
                throw new FileNotFoundException("ACCustoms.Native.dll was not found next to ACCustoms.dll.", nativePath);

            module = LoadLibrary(nativePath);
            if (module == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "LoadLibrary failed for " + nativePath);

            initialize = Resolve<NativeCall>("ACCustoms_Initialize");
            applyTestReplacement = Resolve<NativeCall>("ACCustoms_ApplyTestReplacement");
            restoreVanilla = Resolve<NativeCall>("ACCustoms_RestoreVanilla");
            getActiveMode = Resolve<NativeCall>("ACCustoms_GetActiveMode");
            setDeveloperTests = Resolve<NativeSetFlag>("ACCustoms_SetDeveloperTests");
            setReplacementDirectory = Resolve<NativeSetDirectory>("ACCustoms_SetReplacementDirectory");
            setCaptureDirectory = Resolve<NativeSetDirectory>("ACCustoms_SetCaptureDirectory");
            captureUiSnapshot = Resolve<NativeSetDirectory>("ACCustoms_CaptureUiSnapshot");
            captureControlStateLinks = Resolve<NativeSetDirectory>("ACCustoms_CaptureControlStateLinks");
            getHoveredTexture = Resolve<NativeGetHoveredTexture>("ACCustoms_GetHoveredTexture");
            getHoveredTextureStack = Resolve<NativeGetHoveredTextureStack>("ACCustoms_GetHoveredTextureStack");
            shutdown = Resolve<NativeCall>("ACCustoms_Shutdown");
        }

        public static bool Initialize()
        {
            lock (Sync)
            {
                EnsureLoaded();
                return initialize() != 0;
            }
        }

        public static bool ApplyReplacement()
        {
            lock (Sync)
            {
                EnsureLoaded();
                return applyTestReplacement() != 0;
            }
        }

        public static bool RestoreVanilla()
        {
            lock (Sync)
            {
                if (module == IntPtr.Zero)
                    return true;
                return restoreVanilla() != 0;
            }
        }

        public static bool SetReplacementDirectory(string directory)
        {
            lock (Sync)
            {
                EnsureLoaded();
                return setReplacementDirectory(directory) != 0;
            }
        }

        public static bool SetCaptureDirectory(string directory)
        {
            lock (Sync)
            {
                EnsureLoaded();
                return setCaptureDirectory(directory) != 0;
            }
        }

        public static bool SetDeveloperTests(bool enabled)
        {
            lock (Sync)
            {
                EnsureLoaded();
                return setDeveloperTests(enabled ? 1 : 0) != 0;
            }
        }

        public static bool CaptureUiSnapshot(string outputPath)
        {
            lock (Sync)
            {
                EnsureLoaded();
                return captureUiSnapshot(outputPath) != 0;
            }
        }

        public static bool CaptureControlStateLinks(string outputPath)
        {
            lock (Sync)
            {
                EnsureLoaded();
                return captureControlStateLinks(outputPath) != 0;
            }
        }

        public static bool TryGetHoveredTexture(out uint did, out uint width, out uint height)
        {
            lock (Sync)
            {
                did = 0;
                width = 0;
                height = 0;

                if (module == IntPtr.Zero || getHoveredTexture == null)
                    return false;

                return getHoveredTexture(out did, out width, out height) != 0;
            }
        }

        public static string GetHoveredTextureStack()
        {
            lock (Sync)
            {
                if (module == IntPtr.Zero || getHoveredTextureStack == null)
                    return String.Empty;

                StringBuilder buffer = new StringBuilder(4096);
                int written = getHoveredTextureStack(buffer, (uint)buffer.Capacity);
                return written > 0 ? buffer.ToString() : String.Empty;
            }
        }

        public static int GetActiveMode()
        {
            lock (Sync)
            {
                if (module == IntPtr.Zero)
                    return -1;
                return getActiveMode();
            }
        }

        public static void Shutdown()
        {
            lock (Sync)
            {
                if (module != IntPtr.Zero && shutdown != null)
                    shutdown();

                // Deliberately do NOT FreeLibrary here. Native hooks remain
                // installed until acclient.exe exits.
            }
        }
    }
}
