// Plug-in update UI is intentionally separate from the mapping update feature.
// All VVS calls occur on the Decal render thread. I/O is asynchronous.
using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using VirindiViewService.Controls;

namespace ACCustoms
{
    public sealed partial class PluginCore
    {
        private readonly object pluginUpdateLock = new object();
        private PluginRelease availablePluginRelease;
        private PluginRelease pluginCheckedRelease;
        private bool pluginUpdateVisible;
        private bool pluginCheckRunning, pluginCheckPending, pluginCheckManual;
        private string pluginCheckError;
        private bool pluginDownloadRunning, pluginDownloadPending;
        private string pluginDownloadError;
        private bool pluginDownloadedAndScheduled;
        private bool pluginStaged;
        private string pluginUpdateFeedback = String.Empty;
        private DateTime lastPluginCheckUtc = DateTime.MinValue;
        private DateTime nextAutomaticPluginCheckUtc = DateTime.MaxValue;

        private HudButton advancedPluginCheck;
        private HudButton pluginUpdateButton;
        private HudStaticText pluginUpdateHeader, pluginUpdateInstalled,
            pluginUpdateAvailable, pluginUpdateStatus;
        private HudList pluginUpdateNotes;

        private void BeginPluginUpdateCheck(bool manual = false)
        {
            if (updateShuttingDown) return;
            lock (pluginUpdateLock)
            {
                if (pluginCheckRunning) return;
                if (!manual && DateTime.UtcNow - lastPluginCheckUtc < TimeSpan.FromMinutes(15))
                    return;
                pluginCheckRunning = true;
                pluginCheckManual = manual;
                lastPluginCheckUtc = DateTime.UtcNow;
                nextAutomaticPluginCheckUtc = DateTime.UtcNow.AddHours(1);
            }
            if (manual) SetStatus("Checking GitHub for plugin updates...");
            ThreadPool.QueueUserWorkItem(delegate(object state)
            {
                PluginRelease result = null;
                string error = null;
                bool staged = false;
                try
                {
                    result = PluginUpdateService.GetLatest();
                    if (PluginUpdateService.IsNewer(result))
                        staged = PluginUpdateService.IsStaged(result);
                }
                catch (Exception ex) { error = ex.Message; }
                lock (pluginUpdateLock)
                {
                    if (updateShuttingDown) return;
                    pluginCheckedRelease = result;
                    pluginCheckError = error;
                    pluginStaged = staged;
                    pluginCheckPending = true;
                    pluginCheckRunning = false;
                }
            });
        }

        private void AdvancedPluginCheck_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
                BeginPluginUpdateCheck(true);
        }

        private void PluginUpdateButton_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType != ControlMouseEventArgs.MouseEventType.MouseHit ||
                availablePluginRelease == null || !PluginUpdateService.IsNewer(availablePluginRelease))
                return;
            PluginRelease release = availablePluginRelease;
            lock (pluginUpdateLock)
            {
                if (pluginDownloadRunning || pluginDownloadedAndScheduled) return;
                pluginDownloadRunning = true;
            }
            pluginUpdateFeedback = "Downloading and verifying both DLLs...";
            RefreshPluginUpdatePage();
            ThreadPool.QueueUserWorkItem(delegate(object unused)
            {
                string error = null;
                try
                {
                    PluginUpdateService.DownloadAndStage(release);
                    PluginUpdateService.ScheduleInstall(release);
                }
                catch (Exception ex) { error = ex.Message; }
                lock (pluginUpdateLock)
                {
                    if (updateShuttingDown) return;
                    pluginDownloadError = error;
                    pluginDownloadPending = true;
                    pluginDownloadRunning = false;
                }
            });
        }

        private void FinishPluginUpdateWorkers()
        {
            PluginRelease release = null;
            string checkError = null, downloadError = null;
            bool checkedNow = false, downloadedNow = false, manual = false;
            bool stagedNow = false;
            lock (pluginUpdateLock)
            {
                if (pluginCheckPending)
                {
                    checkedNow = true;
                    release = pluginCheckedRelease;
                    checkError = pluginCheckError;
                    manual = pluginCheckManual;
                    stagedNow = pluginStaged;
                    pluginCheckPending = false;
                }
                if (pluginDownloadPending)
                {
                    downloadedNow = true;
                    downloadError = pluginDownloadError;
                    pluginDownloadPending = false;
                }
            }
            if (checkedNow)
            {
                if (release != null)
                {
                    bool newer = PluginUpdateService.IsNewer(release);
                    if (availablePluginRelease == null || availablePluginRelease.Version != release.Version)
                    {
                        pluginUpdateFeedback = String.Empty;
                        pluginDownloadedAndScheduled = false;
                    }
                    availablePluginRelease = release;
                    pluginUpdateVisible = newer;
                    pluginStaged = stagedNow;
                    if (newer && !viewHasPluginUpdateTab && view != null && !rebuildingNotebook)
                        notebookRebuildRequested = true;
                    if (manual)
                        SetStatus(newer ? "Plugin v" + release.Version + " available in Update tab."
                            : "Plugin is current (v" + PluginUpdateService.InstalledVersion + ").");
                    else if (newer)
                        SetStatus("Plugin v" + release.Version + " available in Update tab.");
                }
                else
                {
                    // Network failures leave the existing available release intact.
                    if (manual)
                        SetStatus("Plugin update check failed: " + Shorten(checkError ?? "No response", 100));
                }
            }
            if (downloadedNow)
            {
                if (downloadError == null)
                {
                    pluginDownloadedAndScheduled = true;
                    pluginStaged = true;
                    pluginUpdateFeedback = "Update ready. Installs when AC closes.";
                    SetStatus("Plugin update downloaded. Restart AC to finish.");
                }
                else
                {
                    pluginDownloadedAndScheduled = false;
                    pluginUpdateFeedback = "Update failed: " + Shorten(downloadError, 72);
                    SetStatus(pluginUpdateFeedback);
                }
            }
            if (checkedNow || downloadedNow) RefreshPluginUpdatePage();
        }

        private static List<string> PluginChangelogLines(PluginRelease release)
        {
            var lines = new List<string>();
            if (release == null || release.Changelog == null) return lines;
            foreach (string note in release.Changelog)
            {
                string remaining = (note ?? String.Empty).Replace('\r', ' ').Replace('\n', ' ').Trim();
                bool first = true;
                while (remaining.Length > 0)
                {
                    int take = Math.Min(53, remaining.Length);
                    if (take < remaining.Length)
                    {
                        int space = remaining.LastIndexOf(' ', take, take);
                        if (space >= 12) take = space;
                    }
                    lines.Add((first ? "- " : "  ") + remaining.Substring(0, take).Trim());
                    remaining = remaining.Substring(take).TrimStart();
                    first = false;
                }
            }
            if (lines.Count == 0) lines.Add("See official release information on GitHub.");
            return lines;
        }

        private void RefreshPluginUpdatePage()
        {
            if (view == null || !viewHasPluginUpdateTab || availablePluginRelease == null ||
                pluginUpdateNotes == null) return;
            pluginUpdateHeader.Text = "PLUGIN UPDATE AVAILABLE";
            pluginUpdateInstalled.Text = "Installed: v" + PluginUpdateService.InstalledVersion;
            pluginUpdateAvailable.Text = "Latest: v" + availablePluginRelease.Version;
            while (pluginUpdateNotes.RowCount > 0)
                pluginUpdateNotes.RemoveRow(pluginUpdateNotes.RowCount - 1);
            foreach (string note in PluginChangelogLines(availablePluginRelease))
            {
                HudList.HudListRowAccessor row = pluginUpdateNotes.AddRow();
                HudStaticText text = (HudStaticText)row[0];
                text.Text = note;
                TrySetColor(text, UiBodyText);
            }
            bool downloading;
            lock (pluginUpdateLock) downloading = pluginDownloadRunning;
            if (pluginDownloadedAndScheduled)
            {
                pluginUpdateStatus.Text = "Downloaded. Installs when AC closes.";
                TrySetColor(pluginUpdateStatus, UiGreen);
                ChangeButtonArtworkText("PluginUpdateButton", "Ready for Restart");
            }
            else
            {
                pluginUpdateStatus.Text = String.IsNullOrWhiteSpace(pluginUpdateFeedback)
                    ? pluginStaged ? "Download verified. Click Update to install." :
                        "Ready to download both DLLs."
                    : Shorten(pluginUpdateFeedback, 66);
                TrySetColor(pluginUpdateStatus, UiMuted);
                ChangeButtonArtworkText("PluginUpdateButton", downloading ? "Downloading..." :
                    pluginStaged ? "Complete Update" : "Update Plugin");
            }
            // Keep the button visible for download/progress/success feedback.
            // The handler itself ignores clicks while busy or already scheduled.
            TrySetVvsVisibility(pluginUpdateButton, true);
            try
            {
                var enabled = pluginUpdateButton.GetType().GetProperty("Enabled");
                if (enabled != null && enabled.CanWrite && enabled.PropertyType == typeof(bool))
                    enabled.SetValue(pluginUpdateButton,
                        !downloading && !pluginDownloadedAndScheduled, null);
            }
            catch { /* VVS legacy builds may not have an Enabled setter. */ }
        }
    }
}
