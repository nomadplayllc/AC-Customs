using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using VirindiViewService;
using VirindiViewService.Controls;
using System.Runtime.InteropServices;
using Decal.Adapter;

namespace ACCustoms
{
    [WireUpBaseEvents]
    [FriendlyName("AC Customs")]
    public sealed class PluginCore : PluginBase
    {
        private static readonly TimeSpan ApplyCooldown = TimeSpan.FromSeconds(3);
        private const int CustomIconResourceId = 101;

        private DateTime lastChangeUtc = DateTime.MinValue;
        private bool nativeReady;
        private HudView view;
        private HudButton ImportPack;
        private HudButton RestoreVanilla;
        private DateTime nextViewAttemptUtc;
        private bool viewFailed;
        private string status = "Starting...";
        private bool suppressThemeEvents;
        private string activePackInstallId = String.Empty;
        private List<InstalledPack> installedPacks = new List<InstalledPack>();

        private HudStaticText CurrentUiText = null;

        private HudStaticText StatusText = null;
        private HudStaticText StatusTextLine2 = null;
        private HudStaticText StatusTextLine3 = null;

        private HudCombo ThemeChoice = null;

        private HudButton ApplyTheme = null;

        protected override void Startup()
        {
            try
            {
                Core.RenderFrame += Core_RenderFrame;
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
            try { DisposeView(); } catch { }
            try { NativeBridge.Shutdown(); } catch { }
        }

        [BaseEvent("LoginComplete", "CharacterFilter")]
        private void CharacterFilter_LoginComplete(object sender, EventArgs e)
        {
            try
            {
                if (view == null)
                    Host.Actions.AddChatText("AC Customs: waiting for its VVS window. Ensure Virindi View Service is installed and enabled in Decal Services; restart AC after enabling it.", 3);
            }
            catch { }
        }

        private void SetStatus(string text)
        {
            status = text ?? String.Empty;
            UpdateStatusText();
        }

        private void UpdateStatusText()
        {
            if (StatusText == null) return;
            // VVS static labels are single-line. Use explicit rows instead of
            // depending on native Decal's multiline layout behavior.
            HudStaticText[] rows = { StatusText, StatusTextLine2, StatusTextLine3 };
            string remaining = status.Replace("\r", " ").Replace("\n", " ").Trim();
            for (int i = 0; i < rows.Length; i++)
            {
                int length = Math.Min(44, remaining.Length);
                if (remaining.Length > length && i < rows.Length - 1)
                {
                    int space = remaining.LastIndexOf(' ', length - 1, length);
                    if (space > 0) length = space;
                }
                string line = remaining.Substring(0, length);
                remaining = remaining.Substring(length).TrimStart();
                if (i == rows.Length - 1 && remaining.Length > 0)
                    line = line.Substring(0, Math.Min(41, line.Length)) + "...";
                if (rows[i] != null) rows[i].Text = line;
            }
        }

        private void TryApplyCustomViewIcon()
        {
            // Keep the numeric icon in mainView.xml as the safe load-time fallback.
            // Once the VVS view exists, replace it from a native ICON
            // resource embedded in ACCustoms.dll. Any failure is deliberately
            // swallowed so icon handling can never prevent the plugin UI loading.
            try
            {
                if (view == null)
                    return;

                IntPtr moduleHandle = Marshal.GetHINSTANCE(typeof(PluginCore).Module);
                if (moduleHandle == IntPtr.Zero || moduleHandle == new IntPtr(-1))
                    return;

                view.Icon = ACImage.FromIconLibrary(CustomIconResourceId, unchecked((int)moduleHandle.ToInt64()));
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

            if (currentName.Length > 30)
                currentName = currentName.Substring(0, 27) + "...";

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

        private void CreateView()
        {
            ViewProperties properties;
            ControlGroup controls;
            // Read explicitly so resource lookup does not depend on the calling assembly.
            using (Stream stream = typeof(PluginCore).Assembly.GetManifestResourceStream("ACCustoms.mainView.xml"))
            using (StreamReader reader = new StreamReader(stream))
                new VirindiViewService.XMLParsers.Decal3XMLParser().Parse(reader.ReadToEnd(), out properties, out controls);

            view = new HudView(properties, controls, "ACCustoms.Main");
            view.ClientArea = new Size(420, 374);
            CurrentUiText = (HudStaticText)view["CurrentUiText"];
            StatusText = (HudStaticText)view["StatusText"];
            StatusTextLine2 = (HudStaticText)view["StatusTextLine2"];
            StatusTextLine3 = (HudStaticText)view["StatusTextLine3"];
            ThemeChoice = (HudCombo)view["ThemeChoice"];
            ApplyTheme = (HudButton)view["ApplyTheme"];
            ImportPack = (HudButton)view["ImportPack"];
            RestoreVanilla = (HudButton)view["RestoreVanilla"];
            ThemeChoice.Change += ThemeChoice_Change;
            ApplyTheme.MouseEvent += ApplyTheme_MouseEvent;
            ImportPack.MouseEvent += ImportPack_MouseEvent;
            RestoreVanilla.MouseEvent += RestoreVanilla_MouseEvent;
            TryApplyCustomViewIcon();
            RefreshInstalledPacks(PackManager.LoadLastSelectedInstallId());
            if (nativeReady)
                SetStatus(BuildReadyStatus());
            else
                UpdateStatusText();
        }

        private void DisposeView()
        {
            if (ThemeChoice != null) ThemeChoice.Change -= ThemeChoice_Change;
            if (ApplyTheme != null) ApplyTheme.MouseEvent -= ApplyTheme_MouseEvent;
            if (ImportPack != null) ImportPack.MouseEvent -= ImportPack_MouseEvent;
            if (RestoreVanilla != null) RestoreVanilla.MouseEvent -= RestoreVanilla_MouseEvent;
            if (view != null) view.Dispose();
            view = null;
            CurrentUiText = null;
            StatusText = StatusTextLine2 = StatusTextLine3 = null;
            ThemeChoice = null;
            ApplyTheme = ImportPack = RestoreVanilla = null;
        }

        private void Core_RenderFrame(object sender, EventArgs e)
        {
            if (view != null || viewFailed || DateTime.UtcNow < nextViewAttemptUtc)
                return;
            nextViewAttemptUtc = DateTime.UtcNow.AddSeconds(1);
            try
            {
                // VVS may start after this plugin. Wait for it rather than falling
                // back to a window hidden by Disable View Rendering.
                if (!Service.Running)
                    return;
                CreateView();
            }
            catch (Exception ex)
            {
                viewFailed = true;
                try { DisposeView(); } catch { }
                try { Host.Actions.AddChatText("AC Customs: VVS window failed: " + ex.Message, 3); } catch { }
            }
        }

        private void ApplyTheme_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
                ApplyTheme_Click(sender, EventArgs.Empty);
        }

        private void ImportPack_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
                ImportPack_Click(sender, EventArgs.Empty);
        }

        private void RestoreVanilla_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
                RestoreVanilla_Click(sender, EventArgs.Empty);
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
                    ThemeChoice.AddItem(pack.DisplayName, null);

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

                if (selectedIndex >= 0)
                    ThemeChoice.Current = selectedIndex;
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

            int index = ThemeChoice.Current;
            return index >= 0 && index < installedPacks.Count
                ? installedPacks[index]
                : null;
        }

        private void UpdateApplyButton()
        {
            // The selector already displays the pack name. Keep the action label
            // short so long pack names cannot clip the button caption.
            if (ApplyTheme != null) ApplyTheme.Text = "Apply Theme";
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

        private void ThemeChoice_Change(object sender, EventArgs e)
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

        private void ImportPack_Click(object sender, EventArgs e)
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

        private void ApplyTheme_Click(object sender, EventArgs e)
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

        private void RestoreVanilla_Click(object sender, EventArgs e)
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
