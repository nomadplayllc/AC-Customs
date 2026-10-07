using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.InteropServices;
using Decal.Adapter;
using Decal.Adapter.Wrappers;

namespace ACCustoms
{
    [WireUpBaseEvents]
    [FriendlyName("AC Customs"), View("ACCustoms.mainView.xml")]
    public sealed class PluginCore : PluginBase
    {
        private static readonly TimeSpan ApplyCooldown = TimeSpan.FromSeconds(3);
        private const int RequiredViewWidth = 380;
        private const int RequiredViewHeight = 374;
        private const int CustomIconResourceId = 101;

        private DateTime lastChangeUtc = DateTime.MinValue;
        private bool nativeReady;
        private bool viewSizeEnsured;
        private bool suppressThemeEvents;
        private string activePackInstallId = String.Empty;
        private List<InstalledPack> installedPacks = new List<InstalledPack>();

        [ControlReference("CurrentUiText")]
        private StaticWrapper CurrentUiText = null;

        [ControlReference("StatusText")]
        private StaticWrapper StatusText = null;

        [ControlReference("ThemeChoice")]
        private ChoiceWrapper ThemeChoice = null;

        [ControlReference("ApplyTheme")]
        private PushButtonWrapper ApplyTheme = null;

        protected override void Startup()
        {
            try
            {
                EnsureViewSize();
                TryApplyCustomViewIcon();
                PackManager.EnsureDirectories();

                nativeReady = NativeBridge.Initialize();
                if (nativeReady)
                {
                    // Every acclient.exe process starts vanilla. Point the lazy
                    // replacement fast path at an intentionally empty directory
                    // until the user explicitly presses Apply.
                    NativeBridge.SetCaptureDirectory(PackManager.CaptureDirectory);
                    NativeBridge.SetReplacementDirectory(PackManager.EmptyThemeDirectory);

                    // Beta builds keep all developer diagnostics disabled. The
                    // native exports remain available for internal development,
                    // but no developer controls are exposed in the plugin UI.
                    NativeBridge.SetDeveloperTests(false);
                }

                RefreshInstalledPacks(PackManager.LoadLastSelectedInstallId());
                UpdateCurrentUiText();

                Core.RenderFrame += Core_RenderFrame;
                SetStatus(nativeReady
                    ? BuildReadyStatus()
                    : "Native engine failed to initialize");
            }
            catch (Exception ex)
            {
                nativeReady = false;
                SetStatus("Startup error: " + ex.Message);
            }
        }

        protected override void Shutdown()
        {
            try { Core.RenderFrame -= Core_RenderFrame; } catch { }
            try { NativeBridge.Shutdown(); } catch { }
        }

        private void SetStatus(string text)
        {
            if (StatusText != null)
                StatusText.Text = text ?? String.Empty;
        }

        private void TryApplyCustomViewIcon()
        {
            // Keep the numeric icon in mainView.xml as the safe load-time fallback.
            // Once the classic Decal view exists, replace it from a native ICON
            // resource embedded in ACCustoms.dll. Any failure is deliberately
            // swallowed so icon handling can never prevent the plugin UI loading.
            try
            {
                if (DefaultView == null)
                    return;

                IntPtr moduleHandle = Marshal.GetHINSTANCE(typeof(PluginCore).Module);
                if (moduleHandle == IntPtr.Zero || moduleHandle == new IntPtr(-1))
                    return;

                DefaultView.SetIcon(CustomIconResourceId, unchecked((int)moduleHandle.ToInt64()));
            }
            catch
            {
                // Leave the XML fallback icon intact.
            }
        }

        private string BuildReadyStatus()
        {
            InstalledPack selected = GetSelectedPack();
            if (selected == null)
                return "Ready. Import a UI pack to begin.";

            return "Ready. Selected pack: " + selected.Name;
        }

        private void UpdateCurrentUiText()
        {
            if (CurrentUiText == null)
                return;

            string currentName = "AC Default";

            try
            {
                if (nativeReady &&
                    NativeBridge.GetActiveMode() == 1 &&
                    !String.IsNullOrEmpty(activePackInstallId))
                {
                    for (int i = 0; i < installedPacks.Count; ++i)
                    {
                        if (String.Equals(
                                installedPacks[i].InstallId,
                                activePackInstallId,
                                StringComparison.Ordinal))
                        {
                            currentName = installedPacks[i].Name ?? "Custom UI";
                            break;
                        }
                    }

                    if (currentName == "AC Default")
                        currentName = "Custom UI";
                }
            }
            catch
            {
                // The display is informational only. If the native mode cannot
                // be queried, keep the safest/default label rather than allowing
                // a UI update to affect plugin operation.
            }

            if (currentName.Length > 34)
                currentName = currentName.Substring(0, 31) + "...";

            CurrentUiText.Text = "Current UI: " + currentName;
        }

        private bool BeginChange()
        {
            DateTime now = DateTime.UtcNow;
            TimeSpan elapsed = now - lastChangeUtc;
            if (elapsed < ApplyCooldown)
            {
                int remainingMs = (int)Math.Ceiling((ApplyCooldown - elapsed).TotalMilliseconds);
                SetStatus("Cooldown - wait " + remainingMs + " ms");
                return false;
            }

            lastChangeUtc = now;
            return true;
        }

        private void EnsureViewSize()
        {
            try
            {
                if (DefaultView == null)
                    return;

                Rectangle current = DefaultView.Position;
                int width = Math.Max(current.Width, RequiredViewWidth);
                int height = Math.Max(current.Height, RequiredViewHeight);
                if (current.Width != width || current.Height != height)
                    DefaultView.Position = new Rectangle(current.X, current.Y, width, height);

                Rectangle verified = DefaultView.Position;
                viewSizeEnsured = verified.Width >= RequiredViewWidth &&
                                  verified.Height >= RequiredViewHeight;
            }
            catch { }
        }

        private void Core_RenderFrame(object sender, EventArgs e)
        {
            // Decal can finish sizing a view after Startup. Keep this tiny
            // fallback so the beta layout is guaranteed enough room without
            // retaining the old developer polling loop.
            if (!viewSizeEnsured)
                EnsureViewSize();
        }

        private void RefreshInstalledPacks(string preferredInstallId)
        {
            installedPacks = PackManager.GetInstalledPacks();

            if (ThemeChoice == null)
                return;

            suppressThemeEvents = true;
            try
            {
                ThemeChoice.Clear();
                foreach (InstalledPack pack in installedPacks)
                    ThemeChoice.Add(pack.DisplayName, null);

                int selectedIndex = -1;
                if (!String.IsNullOrEmpty(preferredInstallId))
                {
                    for (int i = 0; i < installedPacks.Count; ++i)
                    {
                        if (String.Equals(installedPacks[i].InstallId, preferredInstallId, StringComparison.Ordinal))
                        {
                            selectedIndex = i;
                            break;
                        }
                    }
                }

                if (selectedIndex < 0 && installedPacks.Count > 0)
                    selectedIndex = 0;

                ThemeChoice.Selected = selectedIndex;
            }
            finally
            {
                suppressThemeEvents = false;
            }

            UpdateApplyButton();
            UpdateCurrentUiText();
        }

        private InstalledPack GetSelectedPack()
        {
            if (ThemeChoice == null)
                return null;

            int index = ThemeChoice.Selected;
            return index >= 0 && index < installedPacks.Count
                ? installedPacks[index]
                : null;
        }

        private void UpdateApplyButton()
        {
            if (ApplyTheme == null)
                return;

            InstalledPack pack = GetSelectedPack();
            if (pack == null)
            {
                ApplyTheme.Text = "Apply Theme";
                return;
            }

            string name = pack.Name ?? "Theme";
            if (name.Length > 24)
                name = name.Substring(0, 21) + "...";
            ApplyTheme.Text = "Apply " + name;
        }

        private bool RestoreActiveThemeInternal(bool resetToEmptyDirectory)
        {
            if (!nativeReady || String.IsNullOrEmpty(activePackInstallId))
                return true;

            bool restored = NativeBridge.RestoreVanilla();
            if (!restored)
            {
                UpdateCurrentUiText();
                return false;
            }

            activePackInstallId = String.Empty;
            if (resetToEmptyDirectory)
                NativeBridge.SetReplacementDirectory(PackManager.EmptyThemeDirectory);

            UpdateCurrentUiText();
            return true;
        }

        [ControlEvent("ThemeChoice", "Change")]
        private void ThemeChoice_Change(object sender, IndexChangeEventArgs e)
        {
            try
            {
                if (suppressThemeEvents)
                    return;

                InstalledPack pack = GetSelectedPack();
                UpdateApplyButton();
                if (pack == null)
                    return;

                PackManager.SaveLastSelectedInstallId(pack.InstallId);
                SetStatus(String.IsNullOrEmpty(activePackInstallId)
                    ? "Selected pack: " + pack.Name
                    : "Current UI unchanged. Selected for next Apply: " + pack.Name);
            }
            catch (Exception ex)
            {
                SetStatus("Theme selection error: " + ex.Message);
            }
        }

        [ControlEvent("ImportPack", "Click")]
        private void ImportPack_Click(object sender, ControlEventArgs e)
        {
            try
            {
                using (System.Windows.Forms.OpenFileDialog dialog = new System.Windows.Forms.OpenFileDialog())
                {
                    dialog.Filter = "AC Customs UI Packs (*.acui)|*.acui|All Files (*.*)|*.*";
                    dialog.Title = "Import AC Customs UI Pack";
                    dialog.CheckFileExists = true;
                    dialog.Multiselect = false;

                    if (dialog.ShowDialog() != System.Windows.Forms.DialogResult.OK)
                        return;

                    SetStatus("Validating UI pack...");
                    PackInspection inspection = PackManager.InspectPackage(dialog.FileName);

                    if (inspection.AlreadyInstalled)
                    {
                        System.Windows.Forms.DialogResult overwrite = System.Windows.Forms.MessageBox.Show(
                            "A UI pack with this name and author is already installed.\r\n\r\n" +
                            "Replace the installed copy?",
                            "AC Customs - Import UI Pack",
                            System.Windows.Forms.MessageBoxButtons.YesNo,
                            System.Windows.Forms.MessageBoxIcon.Question,
                            System.Windows.Forms.MessageBoxDefaultButton.Button2);

                        if (overwrite != System.Windows.Forms.DialogResult.Yes)
                        {
                            SetStatus(BuildReadyStatus());
                            return;
                        }

                        // If the installed copy being replaced is the active
                        // runtime directory, return to vanilla before swapping it.
                        if (String.Equals(activePackInstallId, inspection.InstallId, StringComparison.Ordinal))
                        {
                            if (!BeginChange())
                                return;
                            SetStatus("Restoring AC Default before updating active pack...");
                            if (!RestoreActiveThemeInternal(true))
                            {
                                SetStatus("Could not safely restore AC Default; import cancelled");
                                return;
                            }
                        }
                    }

                    SetStatus("Installing " + inspection.Manifest.Name + "...");
                    InstalledPack installed = PackManager.InstallPackage(inspection, inspection.AlreadyInstalled);
                    PackManager.SaveLastSelectedInstallId(installed.InstallId);
                    RefreshInstalledPacks(installed.InstallId);
                    SetStatus(String.IsNullOrEmpty(activePackInstallId)
                        ? "Imported " + installed.Name + " - AC Default remains active"
                        : "Imported " + installed.Name + " - current active theme is unchanged");
                }
            }
            catch (Exception ex)
            {
                SetStatus("Import failed: " + ex.Message);
            }
        }

        [ControlEvent("ApplyTheme", "Click")]
        private void ApplyTheme_Click(object sender, ControlEventArgs e)
        {
            try
            {
                if (!nativeReady)
                {
                    SetStatus("Native engine is not ready");
                    return;
                }

                InstalledPack selected = GetSelectedPack();
                if (selected == null)
                {
                    SetStatus("No UI pack selected. Import a .acui pack first.");
                    return;
                }

                if (String.Equals(activePackInstallId, selected.InstallId, StringComparison.Ordinal) &&
                    NativeBridge.GetActiveMode() == 1)
                {
                    UpdateCurrentUiText();
                    SetStatus(selected.Name + " is already active");
                    return;
                }

                if (!BeginChange())
                    return;

                // Theme A -> Theme B is intentionally Vanilla -> B. This ensures
                // DIDs present only in A are restored instead of leaking into B.
                if (!String.IsNullOrEmpty(activePackInstallId))
                {
                    SetStatus("Restoring AC Default before switching UIs...");
                    if (!RestoreActiveThemeInternal(false))
                    {
                        SetStatus("Restore incomplete; UI switch aborted");
                        return;
                    }
                }

                if (!NativeBridge.SetReplacementDirectory(selected.TexturesDirectory))
                {
                    SetStatus("Could not select theme directory");
                    return;
                }

                SetStatus("Applying " + selected.Name + "...");
                bool ok = NativeBridge.ApplyReplacement();
                if (NativeBridge.GetActiveMode() == 1)
                    activePackInstallId = selected.InstallId;

                UpdateCurrentUiText();

                if (!ok)
                {
                    SetStatus("Apply incomplete - restore AC Default or restart AC");
                    return;
                }

                activePackInstallId = selected.InstallId;
                PackManager.SaveLastSelectedInstallId(selected.InstallId);
                UpdateCurrentUiText();
                SetStatus(selected.Name + " active");
            }
            catch (Exception ex)
            {
                SetStatus("Apply error: " + ex.Message);
            }
        }

        [ControlEvent("RestoreVanilla", "Click")]
        private void RestoreVanilla_Click(object sender, ControlEventArgs e)
        {
            try
            {
                if (!nativeReady || String.IsNullOrEmpty(activePackInstallId))
                {
                    UpdateCurrentUiText();
                    SetStatus(BuildReadyStatus());
                    return;
                }

                if (!BeginChange())
                    return;

                SetStatus("Restoring AC Default UI...");
                bool ok = RestoreActiveThemeInternal(true);
                UpdateCurrentUiText();
                SetStatus(ok
                    ? "AC Default restored."
                    : "Restore incomplete - close and restart AC if problems continue.");
            }
            catch (Exception ex)
            {
                SetStatus("Restore error: " + ex.Message);
            }
        }

    }
}
