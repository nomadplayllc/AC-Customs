using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Diagnostics;
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
        // One palette for the MAIN window AND the MAPPING UPDATE window.
        // Never duplicate ARGB literals across the two appearance methods.
        private static readonly Color UiBackground = Color.FromArgb(255, 13, 20, 30);
        private static readonly Color UiGold = Color.FromArgb(223, 174, 85);
        private static readonly Color UiBlue = Color.FromArgb(100, 168, 222);
        private static readonly Color UiMuted = Color.FromArgb(175, 192, 212);
        private static readonly Color UiBodyText = Color.FromArgb(190, 207, 223);
        private static readonly Color UiButtonText = Color.FromArgb(245, 247, 250);
        private static readonly Color UiGreen = Color.FromArgb(82, 210, 133);
        private static readonly Color UiGenerateButton = Color.FromArgb(145, 100, 37);
        private static readonly Color UiPrimaryButton = Color.FromArgb(39, 89, 142);
        private static readonly Color UiSecondaryButton = Color.FromArgb(29, 43, 58);
        private static readonly Color UiAdvancedButton = Color.FromArgb(20, 32, 47);
        private static readonly Color UiUpdateButton = Color.FromArgb(126, 87, 34);
        private const int CustomIconResourceId = 101;

        private DateTime lastChangeUtc = DateTime.MinValue;
        private bool nativeReady;
        private HudView view;
        private HudButton ImportPack;
        private HudButton RestoreVanilla;
        // Native VVS notebook: real tabs, no tab-shaped buttons or extra VVS icon.
        // VVS creates one notebook in one HudView, just like other Decal plugins.
        private bool viewHasUpdateTab;
        private bool notebookRebuildRequested;
        private bool rebuildingNotebook;
        private Size lastNotebookClientArea = Size.Empty;
        private DateTime nextNotebookLayoutPollUtc = DateTime.MinValue;
        private readonly HashSet<string> notebookLayoutApplied = new HashSet<string>();
        // Each Notebook page owns an independently painted FixedLayout.
        // Setting the root layout's Image alone allows light VVS themes to
        // paint gray over our navy surface (and make pale text unreadable).
        private readonly HashSet<string> skinnedPageLayouts = new HashSet<string>();
        private sealed class ButtonArtwork
        {
            internal HudButton Control;
            internal string Caption;
            internal Color Background;
            internal int Width, Height, FontPixels;
            internal bool Bold;
        }
        // VVS skins can ignore HudButton.TextColor. Keep the captions inside
        // our OWN button bitmap so skin-specific black text cannot override it.
        private readonly Dictionary<string, ButtonArtwork> buttonArtwork =
            new Dictionary<string, ButtonArtwork>();
        private readonly List<Bitmap> ownedButtonBitmaps = new List<Bitmap>();
        // Official themes are independent of mapping versions. The GitHub
        // contents API supplies file SHA identifiers; there is no theme catalog.
        private readonly object themeWorkerLock = new object();
        private readonly List<GithubThemeItem> availableThemes = new List<GithubThemeItem>();
        private int selectedOfficialTheme = -1;
        private bool themeCatalogRunning, themeCatalogReady, themeCatalogPending;
        private List<GithubThemeItem> themeCatalogResult;
        private string themeCatalogError;
        private DateTime lastThemeCheckUtc = DateTime.MinValue;
        private DateTime nextThemeRetryUtc = DateTime.MaxValue;
        private bool themeInstallRunning, themeInstallPending;
        private string themeInstallPath, themeInstallError;
        private GithubThemeItem installingTheme;
        private bool uploadRunning, uploadPending;
        private string uploadResultId, uploadResultError, selectedUploadPng = String.Empty;
        private string themesFeedback = "Checking GitHub themes...";
        private string uploadFeedback = "Select a PNG to prepare a submission.";
        private HudList themesList;
        private HudStaticText themesHeader, themesIntro, themesStatus;
        private HudButton themesRefresh, themesInstall, themesUse;
        private HudStaticText uploadHeader, uploadIntro, uploadFilename, uploadGuidelines, uploadStatus;
        private HudButton uploadBrowse, uploadSubmit;

        private HudButton AdvancedUseDefault;
        private HudStaticText AdvancedMappingLabel;
        private HudStaticText AdvancedMappingFile;
        private HudStaticText AdvancedMappingHint;
        private HudList updateNotesList;
        private HudButton AdvancedCheckUpdates;
        private HudStaticText AdvancedHeader;
        private HudStaticText AdvancedInfo;
        private HudStaticText AdvancedMappingVersion;
        private HudStaticText AdvancedFooter;
        private HudStaticText UpdateHeader;
        private HudStaticText UpdateNotesHeader;
        private HudStaticText UpdateInstructions;
        private string updateFeedback = String.Empty;
        private HudStaticText updateInstalledText;
        private HudStaticText updateAvailableText;
        private HudStaticText updateStatusText;
        private HudButton updateDownloadButton;
        private HudButton updateOpenFolderButton;
        private MappingRelease availableMappingRelease;
        private string currentMappingReleaseName = "Bundled";
        private string downloadedReleaseFolder = String.Empty;
        private bool mappingUpdateVisible;
        private bool updateShuttingDown;
        private readonly object mappingUpdateWorkerLock = new object();
        private bool mappingCheckRunning;
        private bool mappingCheckResultPending;
        private MappingRelease checkedMappingRelease;
        private string mappingCheckError;
        private bool manualMappingCheckPending;
        private static readonly object mappingUpdateLogLock = new object();
        private static readonly string MappingUpdateLogPath = System.IO.Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ACCustoms", "mapping-update.log");
        private DateTime lastMappingCheckUtc = DateTime.MinValue;
        // A brief network failure during AC/Decal startup should not leave a
        // red error in the plugin or require opening Advanced to retry.
        private DateTime nextAutomaticMappingRetryUtc = DateTime.MaxValue;
        private int automaticMappingFailures;
        private bool mappingDownloadRunning;
        private bool mappingDownloadResultPending;
        private string mappingDownloadFolderResult;
        private string mappingDownloadError;
        private MappingRelease mappingDownloadReleaseResult;
        // Defer activation if a texture compile starts during the download.
        // These two fields are only read/written on the Decal render thread.
        private MappingRelease pendingMappingActivation;
        private string pendingMappingFolder = String.Empty;

        private DateTime nextViewAttemptUtc;
        private bool viewFailed;
        private string status = "Starting...";
        private bool suppressThemeEvents;
        private string activePackInstallId = String.Empty;
        // The display name is captured on a successful Apply. Choosing a new
        // theme file does not change the name of the UI already active in AC.
        private string activeThemeDisplayName = String.Empty;
        private List<InstalledPack> installedPacks = new List<InstalledPack>();

        private HudStaticText CurrentUiText = null;

        private HudStaticText StatusText = null;
        private HudStaticText StatusTextLine2 = null;
        private HudStaticText StatusTextLine3 = null;

        private HudCombo ThemeChoice = null;

        private HudButton ApplyTheme = null;

        // Generated themes use the same texture-directory and Apply/Restore
        // mechanism as imported .acui packs. Never swap an active directory.
        private const string GeneratedTemplateInstallId = "Generated_Master_Template";
        private static readonly string TemplateSourceSettingsPath = System.IO.Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ACCustoms", "master-template-source.txt");
        private static readonly string GeneratedTemplateDirectory = System.IO.Path.Combine(
            PackManager.PacksDirectory, GeneratedTemplateInstallId);
        private static readonly string MappingSettingsPath = System.IO.Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
            "ACCustoms", "master-template-mapping.txt");
        // Keep the embedded mapping as an offline fallback. Newer official
        // mappings are downloaded under LocalAppData and their selected path is
        // persisted to MappingSettingsPath; they do not require a new DLL.
        private const string BundledMappingResourceName = "ACCustoms.Assets.template_mapping.json";
        private static readonly string BundledMappingPath = System.IO.Path.Combine(
            PackManager.RuntimeDirectory, "DefaultMapping", "template_mapping.json");

        private string selectedTemplatePng = String.Empty;
        private string selectedMappingJson = String.Empty;
        private HudStaticText TemplatePngText;
        private HudStaticText TemplateStatusIcon;
        private HudStaticText BrandTitle;
        private HudStaticText BrandSubtitle;
        private HudStaticText TemplateLabel;
        private HudStaticText TemplateStatusText;
        private HudStaticText TemplateStatusDetail;
        private HudButton BrowseTemplate;
        // Advanced temporarily reuses the theme picker and Refresh button to
        // configure an optional custom mapping without crowding the main UI.
        private HudButton AdvancedOptions;
        private HudButton RefreshTemplate;
        private HudButton GenerateTemplate;
        private HudButton ApplyGeneratedTemplate;
        private readonly object templateWorkerLock = new object();
        private bool templateBuildRunning;
        private TemplateBuildReport completedTemplateReport;
        private string completedTemplateError;
        private volatile bool templateBuildResultPending;
        // Lightweight timestamp/length polling only. SHA-256 is computed on
        // explicit status refresh, generation and Apply, not every render frame.
        private DateTime nextTemplatePollUtc = DateTime.MinValue;
        private string observedPngStamp = String.Empty;
        private string observedMappingStamp = String.Empty;

        protected override void Startup()
        {
            try
            {
                Core.RenderFrame += Core_RenderFrame;
                PackManager.EnsureDirectories();
                string mappingInstallError;
                bool mappingInstalled = EnsureBundledMapping(out mappingInstallError);
                try
                {
                    if (File.Exists(TemplateSourceSettingsPath))
                        selectedTemplatePng = File.ReadAllText(TemplateSourceSettingsPath).Trim();
                    // Every installation uses the bundled version unless an
                    // explicit, still-existing advanced override is present.
                    selectedMappingJson = BundledMappingPath;
                    if (File.Exists(MappingSettingsPath))
                    {
                        string optionalOverride = File.ReadAllText(MappingSettingsPath).Trim();
                        if (!String.IsNullOrWhiteSpace(optionalOverride) && File.Exists(optionalOverride))
                            selectedMappingJson = System.IO.Path.GetFullPath(optionalOverride);
                    }
                }
                catch (IOException) { selectedMappingJson = BundledMappingPath; }
                catch (UnauthorizedAccessException) { selectedMappingJson = BundledMappingPath; }

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

                SetStatus(!mappingInstalled
                    ? "Bundled mapping error: " + mappingInstallError
                    : nativeReady ? BuildReadyStatus()
                    : "Native engine failed to initialize");

                // LoginComplete triggers another check after character login.
                // Also check once at startup if the plugin loads into an active session.
                BeginMappingUpdateCheck();
                BeginThemeCatalogCheck();
            }
            catch (Exception ex)
            {
                nativeReady = false;
                SetStatus("Startup error: " + ex.Message);
            }
        }

        protected override void Shutdown()
        {
            updateShuttingDown = true;
            try { Core.RenderFrame -= Core_RenderFrame; } catch { }
            try { DisposeView(); } catch { }
            try { NativeBridge.Shutdown(); } catch { }
        }

        // Materialize the embedded mapping only when its contents change.
        // Avoid touching its timestamp every login: template-build receipts
        // detect source changes and would otherwise request regeneration.
        private static bool EnsureBundledMapping(out string error)
        {
            error = String.Empty;
            try
            {
                byte[] embedded;
                using (Stream input = typeof(PluginCore).Assembly.GetManifestResourceStream(
                    BundledMappingResourceName))
                {
                    if (input == null)
                    {
                        error = "This plugin DLL does not contain its default mappings.";
                        return false;
                    }
                    using (var buffer = new MemoryStream())
                    {
                        input.CopyTo(buffer);
                        embedded = buffer.ToArray();
                    }
                }
                if (embedded.Length == 0)
                {
                    error = "Bundled mapping data is empty.";
                    return false;
                }
                Directory.CreateDirectory(System.IO.Path.GetDirectoryName(BundledMappingPath));
                if (File.Exists(BundledMappingPath))
                {
                    byte[] existing = File.ReadAllBytes(BundledMappingPath);
                    if (existing.Length == embedded.Length)
                    {
                        bool same = true;
                        for (int i = 0; i < existing.Length; ++i)
                            if (existing[i] != embedded[i]) { same = false; break; }
                        if (same) return true;
                    }
                }
                string staged = BundledMappingPath + ".tmp";
                File.WriteAllBytes(staged, embedded);
                if (File.Exists(BundledMappingPath))
                    File.Replace(staged, BundledMappingPath, null);
                else
                    File.Move(staged, BundledMappingPath);
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                return false;
            }
        }

        [BaseEvent("LoginComplete", "CharacterFilter")]
        private void CharacterFilter_LoginComplete(object sender, EventArgs e)
        {
            try
            {
                // The same client process can create a new UI desktop after
                // logout/login. Relearn its native UI roots before Apply/Restore.
                if (nativeReady && !NativeBridge.NotifyLoginComplete())
                    Host.Actions.AddChatText("AC Customs: failed to refresh UI discovery after login.", 3);

                if (view == null)
                    Host.Actions.AddChatText("AC Customs: waiting for its VVS window. Ensure Virindi View Service is installed and enabled in Decal Services; restart AC after enabling it.", 3);
                BeginMappingUpdateCheck();
                BeginThemeCatalogCheck();
            }
            catch (Exception ex)
            {
                try { Host.Actions.AddChatText("AC Customs: login UI refresh error: " + ex.Message, 3); }
                catch { }
            }
        }

        private static bool IsWarningMessage(string message)
        {
            if (String.IsNullOrWhiteSpace(message)) return false;
            string value = message.ToLowerInvariant();
            return value.Contains("failed") || value.Contains("error") ||
                value.Contains("missing") || value.Contains("incomplete") ||
                value.Contains("could not") || value.Contains("cannot") ||
                value.Contains("wait ") || value.Contains("choose ") ||
                value.Contains("required") || value.Contains("regenerate") ||
                value.Contains("not ready") || value.Contains("cancelled");
        }

        private void SetStatus(string text)
        {
            status = text ?? String.Empty;
            UpdateStatusText();
            // The Theme status line is hidden on Advanced. Display manual
            // update-check feedback locally on the Advanced page instead.
            if (AdvancedFooter != null)
                AdvancedFooter.Text = Shorten(status.Replace("\r", " ").Replace("\n", " "), 58);
        }

        private void UpdateStatusText()
        {
            if (StatusText == null) return;
            // The lower message is a brief action hint, not a raw compiler enum.
            // Warnings/errors use bright red; normal messages stay muted.
            bool warning = IsWarningMessage(status);
            TrySetColor(StatusText, warning
                ? Color.FromArgb(255, 94, 104)
                : Color.FromArgb(190, 207, 223));
            string message = status.Replace("\r", " ").Replace("\n", " ").Trim();
            StatusText.Text = Shorten(message, 57);
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
            if (String.IsNullOrWhiteSpace(selectedTemplatePng))
                return "Select a theme to begin.";
            if (!File.Exists(selectedMappingJson))
                return "Built-in mapping missing. Reinstall AC Customs.";
            return "Theme selected.";
        }

        private void UpdateCurrentUiText()
        {
            if (CurrentUiText == null) return;
            string name = "AC Default";
            try
            {
                // Each client process starts with the vanilla UI. A theme only
                // counts as active after NativeBridge confirms active mode 1.
                if (nativeReady && NativeBridge.GetActiveMode() == 1 &&
                    !String.IsNullOrEmpty(activePackInstallId))
                {
                    if (!String.IsNullOrWhiteSpace(activeThemeDisplayName))
                        name = activeThemeDisplayName;
                    else if (activePackInstallId == GeneratedTemplateInstallId &&
                             !String.IsNullOrWhiteSpace(selectedTemplatePng))
                        name = System.IO.Path.GetFileNameWithoutExtension(selectedTemplatePng);
                    else
                    {
                        InstalledPack active = installedPacks.Find(p =>
                            String.Equals(p.InstallId, activePackInstallId,
                                StringComparison.Ordinal));
                        name = active != null ? active.Name : "Custom Theme";
                    }
                }
            }
            catch
            {
                // Theme label is presentational. Never break an operation when
                // querying the native mode fails.
            }
            CurrentUiText.Text = "Current Theme: " + Shorten(name, 35);
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
            string layoutXml;
            using (Stream stream = typeof(PluginCore).Assembly.GetManifestResourceStream("ACCustoms.mainView.xml"))
            using (StreamReader reader = new StreamReader(stream))
                layoutXml = reader.ReadToEnd();
            viewHasUpdateTab = mappingUpdateVisible;
            // Hide the entire native tab until there is a real update; parsing
            // the same notebook layout prevents disabled/empty tab placeholders.
            if (!viewHasUpdateTab)
            {
                const string begin = "<!-- AC_UPDATE_PAGE_BEGIN -->";
                const string end = "<!-- AC_UPDATE_PAGE_END -->";
                int first = layoutXml.IndexOf(begin, StringComparison.Ordinal);
                int last = layoutXml.IndexOf(end, StringComparison.Ordinal);
                if (first < 0 || last < first)
                    throw new InvalidDataException("Update notebook template markers missing.");
                layoutXml = layoutXml.Remove(first, last + end.Length - first);
            }
            new VirindiViewService.XMLParsers.Decal3XMLParser().Parse(layoutXml, out properties, out controls);

            view = new HudView(properties, controls, "ACCustoms.Main");
            // XML sets the initial size. The VVS user's stored size is retained.
            // Magellan3's VVS example confirms this property name and that
            // FixedLayout children must be reflowed when the frame resizes.
            EnableNativeWindowResize();
            lastNotebookClientArea = Size.Empty;
            notebookLayoutApplied.Clear();
            skinnedPageLayouts.Clear();
            buttonArtwork.Clear();
            CurrentUiText = (HudStaticText)view["CurrentUiText"];
            StatusText = (HudStaticText)view["StatusText"];
            StatusTextLine2 = null;
            StatusTextLine3 = null;
            BrandTitle = (HudStaticText)view["BrandTitle"];
            BrandSubtitle = null;
            TemplateLabel = (HudStaticText)view["TemplateLabel"];
            TemplatePngText = (HudStaticText)view["TemplatePngText"];
            TemplateStatusIcon = (HudStaticText)view["TemplateStatusIcon"];
            TemplateStatusText = (HudStaticText)view["TemplateStatusText"];
            TemplateStatusDetail = null;
            BrowseTemplate = (HudButton)view["BrowseTemplate"];
            AdvancedOptions = (HudButton)view["AdvancedOptions"];
            AdvancedUseDefault = (HudButton)view["AdvancedUseDefault"];
            AdvancedMappingLabel = (HudStaticText)view["AdvancedMappingLabel"];
            AdvancedMappingFile = (HudStaticText)view["AdvancedMappingFile"];
            AdvancedMappingHint = (HudStaticText)view["AdvancedMappingHint"];
            RefreshTemplate = (HudButton)view["RefreshTemplate"];
            GenerateTemplate = (HudButton)view["GenerateTemplate"];
            ApplyGeneratedTemplate = (HudButton)view["ApplyGeneratedTemplate"];
            RestoreVanilla = (HudButton)view["RestoreVanilla"];
            themesHeader = (HudStaticText)view["ThemesHeader"];
            themesIntro = (HudStaticText)view["ThemesIntro"];
            themesStatus = (HudStaticText)view["ThemesStatus"];
            themesList = (HudList)view["ThemesList"];
            themesRefresh = (HudButton)view["ThemesRefresh"];
            themesInstall = (HudButton)view["ThemesInstall"];
            themesUse = (HudButton)view["ThemesUse"];
            uploadHeader = (HudStaticText)view["UploadHeader"];
            uploadIntro = (HudStaticText)view["UploadIntro"];
            uploadFilename = (HudStaticText)view["UploadFilename"];
            uploadGuidelines = (HudStaticText)view["UploadGuidelines"];
            uploadStatus = (HudStaticText)view["UploadStatus"];
            uploadBrowse = (HudButton)view["UploadBrowse"];
            uploadSubmit = (HudButton)view["UploadSubmit"];
            AdvancedCheckUpdates = (HudButton)view["AdvancedCheckUpdates"];
            AdvancedHeader = (HudStaticText)view["AdvancedHeader"];
            AdvancedInfo = (HudStaticText)view["AdvancedInfo"];
            AdvancedMappingVersion = (HudStaticText)view["AdvancedMappingVersion"];
            AdvancedFooter = (HudStaticText)view["AdvancedFooter"];
            if (viewHasUpdateTab)
            {
                UpdateHeader = (HudStaticText)view["UpdateHeader"];
                UpdateNotesHeader = (HudStaticText)view["UpdateNotesHeader"];
                UpdateInstructions = (HudStaticText)view["UpdateInstructions"];
                updateInstalledText = (HudStaticText)view["UpdateInstalled"];
                updateAvailableText = (HudStaticText)view["UpdateAvailable"];
                updateStatusText = (HudStaticText)view["UpdateDownloadStatus"];
                updateNotesList = (HudList)view["UpdateNotesList"];
                updateOpenFolderButton = (HudButton)view["UpdateOpenFolder"];
                updateDownloadButton = (HudButton)view["UpdateDownload"];
            }

            // DEPRECATED ACUI UI: hidden in favor of the theme + mapping workflow.
            // ImportPack, ThemeChoice, ApplyTheme handlers and PackManager remain
            // below for backward compatibility; remove in a future release.
            ThemeChoice = null;
            ApplyTheme = null;
            ImportPack = null;

            BrowseTemplate.MouseEvent += BrowseTemplate_MouseEvent;
            AdvancedOptions.MouseEvent += AdvancedOptions_MouseEvent;
            AdvancedUseDefault.MouseEvent += AdvancedUseDefault_MouseEvent;
            RefreshTemplate.MouseEvent += RefreshTemplate_MouseEvent;
            GenerateTemplate.MouseEvent += GenerateTemplate_MouseEvent;
            ApplyGeneratedTemplate.MouseEvent += ApplyGeneratedTemplate_MouseEvent;
            RestoreVanilla.MouseEvent += RestoreVanilla_MouseEvent;
            AdvancedCheckUpdates.MouseEvent += AdvancedCheckUpdates_MouseEvent;
            themesList.Click += ThemesList_Click;
            themesRefresh.MouseEvent += ThemesRefresh_MouseEvent;
            themesInstall.MouseEvent += ThemesInstall_MouseEvent;
            themesUse.MouseEvent += ThemesUse_MouseEvent;
            uploadBrowse.MouseEvent += UploadBrowse_MouseEvent;
            uploadSubmit.MouseEvent += UploadSubmit_MouseEvent;
            if (viewHasUpdateTab)
            {
                updateOpenFolderButton.MouseEvent += UpdateOpenFolder_MouseEvent;
                updateDownloadButton.MouseEvent += UpdateDownload_MouseEvent;
            }
            ApplyModernAppearance();
            RefreshMappingUpdateButton();
            RefreshAdvancedMappingInfo();
            if (viewHasUpdateTab) RefreshMappingUpdateWindow();
            TryApplyCustomViewIcon();
            RefreshInstalledPacks(null);
            RefreshTemplateStatus();
            RefreshThemesTab();
            RefreshUploadTab();
            UpdateCurrentUiText();
            SetStatus(nativeReady ? BuildReadyStatus() : "Native engine failed to initialize");
        }

        private static bool TrySetColor(object target, Color color)
        {
            if (target == null) return false;
            // VVS themes may supply their own button caption color. Prefer an
            // explicit foreground rather than leaving black text on dark blue.
            // Use reflection to remain compatible with older VVS builds.
            foreach (string name in new[] { "TextColor", "ForeColor", "FontColor" })
            {
                var property = target.GetType().GetProperty(name);
                if (property != null && property.CanWrite &&
                    property.PropertyType == typeof(Color))
                {
                    try { property.SetValue(target, color, null); return true; }
                    catch { /* Try the next supported property. */ }
                }
            }
            return false;
        }

        private void ApplyModernAppearance()
        {
            ApplySharedWindowBackground();
            TrySetColor(BrandTitle, UiGold);
            TrySetColor(BrandSubtitle, UiBlue);
            TrySetColor(TemplateLabel, UiGold);
            TrySetColor(CurrentUiText, UiMuted);
            TrySetColor(TemplatePngText, UiMuted);
            TrySetColor(StatusText, UiBodyText);
            foreach (object heading in new object[] { AdvancedHeader, AdvancedMappingLabel,
                UpdateHeader, UpdateNotesHeader })
                TrySetColor(heading, UiGold);
            foreach (object info in new object[] { AdvancedInfo, AdvancedMappingVersion,
                AdvancedFooter, AdvancedMappingFile, AdvancedMappingHint,
                UpdateInstructions, updateInstalledText })
                TrySetColor(info, UiMuted);
            foreach (object heading in new object[] { themesHeader, uploadHeader })
                TrySetColor(heading, UiGold);
            foreach (object info in new object[] { themesIntro, uploadIntro, uploadGuidelines,
                uploadFilename }) TrySetColor(info, UiMuted);
            TrySetColor(themesStatus, UiBodyText);
            TrySetColor(uploadStatus, UiBodyText);
            TrySetColor(updateAvailableText, UiBlue);
            TrySetColor(updateStatusText, UiMuted);

            // An ordinary VVS PushButton lets a skin paint its own caption,
            // including black text over our blue fill. Use an ACImage(Bitmap)
            // containing both our fill and caption; keep HudButton for clicks.
            RegisterButtonArtwork("BrowseTemplate", BrowseTemplate, "Browse",
                UiAdvancedButton, 130, 29, 14, true);
            RegisterButtonArtwork("RefreshTemplate", RefreshTemplate, "Refresh",
                UiAdvancedButton, 109, 29, 14, true);
            RegisterButtonArtwork("GenerateTemplate", GenerateTemplate, "GENERATE TEXTURES",
                UiGenerateButton, 242, 37, 16, true);
            RegisterButtonArtwork("ApplyGeneratedTemplate", ApplyGeneratedTemplate, "APPLY UI",
                UiPrimaryButton, 144, 37, 16, true);
            RegisterButtonArtwork("RestoreVanilla", RestoreVanilla, "Restore Default UI",
                UiSecondaryButton, 394, 29, 14, false);
            RegisterButtonArtwork("ThemesRefresh", themesRefresh, "Refresh",
                UiSecondaryButton, 114, 31, 14, false);
            RegisterButtonArtwork("ThemesInstall", themesInstall, "Install/Update",
                UiPrimaryButton, 132, 31, 14, true);
            RegisterButtonArtwork("ThemesUse", themesUse, "Use Theme",
                UiAdvancedButton, 130, 31, 14, true);
            RegisterButtonArtwork("UploadBrowse", uploadBrowse, "Select PNG",
                UiAdvancedButton, 130, 31, 14, false);
            RegisterButtonArtwork("UploadSubmit", uploadSubmit, "Submit for Review",
                UiPrimaryButton, 394, 32, 14, true);
            RegisterButtonArtwork("AdvancedOptions", AdvancedOptions, "Change Mapping",
                UiAdvancedButton, 138, 28, 14, false);
            RegisterButtonArtwork("AdvancedUseDefault", AdvancedUseDefault, "Use Default",
                UiAdvancedButton, 125, 28, 14, false);
            RegisterButtonArtwork("AdvancedCheckUpdates", AdvancedCheckUpdates, "Check Updates",
                UiSecondaryButton, 177, 32, 14, true);
            RegisterButtonArtwork("UpdateOpenFolder", updateOpenFolderButton, "View Files",
                UiSecondaryButton, 82, 29, 13, false);
            RegisterButtonArtwork("UpdateDownload", updateDownloadButton, "Install Update",
                UiPrimaryButton, 107, 29, 13, true);
        }

        private static bool ApplyLayoutBackground(object layout)
        {
            if (layout == null) return false;
            try
            {
                var image = layout.GetType().GetProperty("Image");
                if (image == null || !image.CanWrite ||
                    !image.PropertyType.IsAssignableFrom(typeof(ACImage))) return false;
                image.SetValue(layout, new ACImage(UiBackground), null);
                return true;
            }
            catch { return false; }
        }

        private void ApplySharedWindowBackground()
        {
            if (view == null) return;
            // The outermost FixedLayout no longer paints the visible page now
            // that a Notebook is in front of it. Style each PAGE layout, too.
            if (!skinnedPageLayouts.Contains("__root") &&
                ApplyLayoutBackground(view.Controls.HeadControl))
                skinnedPageLayouts.Add("__root");
            foreach (string name in new[] { "ThemePageLayout", "ThemesPageLayout",
                "UploadPageLayout", "AdvancedPageLayout", "UpdatePageLayout" })
            {
                if (skinnedPageLayouts.Contains(name)) continue;
                try
                {
                    object page = view[name]; // unavailable until a lazy tab is realized
                    if (ApplyLayoutBackground(page)) skinnedPageLayouts.Add(name);
                }
                catch { /* Try again when the tab is realized. */ }
            }
        }

        private void RegisterButtonArtwork(string name, HudButton control,
            string caption, Color background, int width, int height, int fontPixels, bool bold)
        {
            if (control == null) return;
            var item = new ButtonArtwork
            {
                Control = control, Caption = caption, Background = background,
                Width = width, Height = height, FontPixels = fontPixels, Bold = bold
            };
            buttonArtwork[name] = item;
            PaintButtonArtwork(item);
        }

        private void PaintButtonArtwork(ButtonArtwork item)
        {
            if (item == null || item.Control == null) return;
            try
            {
                // Full opaque bitmap avoids transparency compositing differences
                // between installed VVS skins. Text is part of the image, so
                // it cannot inherit black captions from a light theme.
                var bitmap = new Bitmap(item.Width, item.Height,
                    System.Drawing.Imaging.PixelFormat.Format32bppArgb);
                using (Graphics g = Graphics.FromImage(bitmap))
                using (var fill = new SolidBrush(item.Background))
                using (var captionBrush = new SolidBrush(UiButtonText))
                using (var outline = new Pen(Color.FromArgb(125, 147, 166)))
                {
                    g.Clear(item.Background);
                    g.FillRectangle(fill, 0, 0, item.Width, item.Height);
                    g.DrawRectangle(outline, 0, 0, item.Width - 1, item.Height - 1);
                    g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
                    using (var format = new StringFormat())
                    {
                        format.Alignment = StringAlignment.Center;
                        format.LineAlignment = StringAlignment.Center;
                        format.FormatFlags = StringFormatFlags.NoWrap;
                        float fontSize = item.FontPixels;
                        Font font = null;
                        try
                        {
                            while (true)
                            {
                                if (font != null) font.Dispose();
                                font = new Font("Arial", fontSize,
                                    item.Bold ? FontStyle.Bold : FontStyle.Regular,
                                    GraphicsUnit.Pixel);
                                if (g.MeasureString(item.Caption, font).Width <= item.Width - 9 ||
                                    fontSize <= 8.0f) break;
                                fontSize -= 0.5f;
                            }
                            g.DrawString(item.Caption, font, captionBrush,
                                new RectangleF(2, 0, item.Width - 4, item.Height), format);
                        }
                        finally { if (font != null) font.Dispose(); }
                    }
                }
                // ACAudio (open-source VVS plugin) uses this exact constructor
                // on HudButton.Image. Do not release bitmap until view disposal.
                item.Control.Image = new ACImage(bitmap,
                    ACImage.eACImageDrawOptions.DrawStretch);
                ownedButtonBitmaps.Add(bitmap);
                item.Control.Text = String.Empty;
            }
            catch
            {
                // Fall back to a readable skin label if bitmaps are unsupported.
                // Background may still be overridden on that particular VVS build.
                item.Control.Text = item.Caption;
                TrySetColor(item.Control, UiButtonText);
            }
        }

        private void ChangeButtonArtworkText(string name, string text)
        {
            ButtonArtwork item;
            if (buttonArtwork.TryGetValue(name, out item))
            {
                if (item.Caption == text) return;
                item.Caption = text;
                PaintButtonArtwork(item);
            }
        }

        private void ResizeButtonArtwork(string name, int width, int height)
        {
            ButtonArtwork item;
            if (!buttonArtwork.TryGetValue(name, out item) ||
                (item.Width == width && item.Height == height)) return;
            item.Width = width;
            item.Height = height;
            PaintButtonArtwork(item);
        }

        private void SetTemplateVisual(string icon, string title, string detail, Color color)
        {
            if (TemplateStatusIcon != null)
            {
                TemplateStatusIcon.Text = icon == "X" ? "X" : icon == "!" ? "!" : icon == "+" ? "+" : icon == "..." ? "..." : icon == "\u2713" ? "OK" : "R"; // Font-safe glyphs, no Unicode question mark.
                TrySetColor(TemplateStatusIcon, color);
            }
            if (TemplateStatusText != null)
            {
                TemplateStatusText.Text = title;
                TrySetColor(TemplateStatusText, color);
            }
            if (TemplateStatusDetail != null)
            {
                TemplateStatusDetail.Text = Shorten(detail, 61);
                TrySetColor(TemplateStatusDetail, Color.FromArgb(185, 198, 210));
            }
        }

        private void DisposeView()
        {
            if (ThemeChoice != null) ThemeChoice.Change -= ThemeChoice_Change;
            if (ApplyTheme != null) ApplyTheme.MouseEvent -= ApplyTheme_MouseEvent;
            if (ImportPack != null) ImportPack.MouseEvent -= ImportPack_MouseEvent;
            if (RestoreVanilla != null) RestoreVanilla.MouseEvent -= RestoreVanilla_MouseEvent;
            if (AdvancedCheckUpdates != null) AdvancedCheckUpdates.MouseEvent -= AdvancedCheckUpdates_MouseEvent;
            if (themesList != null) themesList.Click -= ThemesList_Click;
            if (themesRefresh != null) themesRefresh.MouseEvent -= ThemesRefresh_MouseEvent;
            if (themesInstall != null) themesInstall.MouseEvent -= ThemesInstall_MouseEvent;
            if (themesUse != null) themesUse.MouseEvent -= ThemesUse_MouseEvent;
            if (uploadBrowse != null) uploadBrowse.MouseEvent -= UploadBrowse_MouseEvent;
            if (uploadSubmit != null) uploadSubmit.MouseEvent -= UploadSubmit_MouseEvent;
            if (updateOpenFolderButton != null) updateOpenFolderButton.MouseEvent -= UpdateOpenFolder_MouseEvent;
            if (updateDownloadButton != null) updateDownloadButton.MouseEvent -= UpdateDownload_MouseEvent;
            if (BrowseTemplate != null) BrowseTemplate.MouseEvent -= BrowseTemplate_MouseEvent;
            if (AdvancedOptions != null) AdvancedOptions.MouseEvent -= AdvancedOptions_MouseEvent;
            if (AdvancedUseDefault != null) AdvancedUseDefault.MouseEvent -= AdvancedUseDefault_MouseEvent;
            if (RefreshTemplate != null) RefreshTemplate.MouseEvent -= RefreshTemplate_MouseEvent;
            if (GenerateTemplate != null) GenerateTemplate.MouseEvent -= GenerateTemplate_MouseEvent;
            if (ApplyGeneratedTemplate != null) ApplyGeneratedTemplate.MouseEvent -= ApplyGeneratedTemplate_MouseEvent;
            if (view != null) view.Dispose();
            view = null;
            buttonArtwork.Clear();
            skinnedPageLayouts.Clear();
            foreach (Bitmap bitmap in ownedButtonBitmaps)
                try { bitmap.Dispose(); } catch { }
            ownedButtonBitmaps.Clear();
            CurrentUiText = null;
            StatusText = StatusTextLine2 = StatusTextLine3 = null;
            ThemeChoice = null;
            ApplyTheme = ImportPack = RestoreVanilla = null;
            TemplatePngText = TemplateStatusIcon = null;
            TemplateStatusText = TemplateStatusDetail = null;
            BrandTitle = BrandSubtitle = TemplateLabel = null;
            BrowseTemplate = RefreshTemplate = GenerateTemplate = ApplyGeneratedTemplate = AdvancedOptions = null;
            AdvancedCheckUpdates = AdvancedUseDefault = null;
            AdvancedMappingLabel = AdvancedMappingFile = AdvancedMappingHint = null;
            AdvancedHeader = AdvancedInfo = AdvancedMappingVersion = AdvancedFooter = null;
            themesHeader = themesIntro = themesStatus = null;
            themesList = null;
            themesRefresh = themesInstall = themesUse = null;
            uploadHeader = uploadIntro = uploadFilename = uploadGuidelines = uploadStatus = null;
            uploadBrowse = uploadSubmit = null;
            UpdateHeader = UpdateNotesHeader = UpdateInstructions = null;
            updateInstalledText = updateAvailableText = updateStatusText = null;
            updateNotesList = null;
            updateOpenFolderButton = updateDownloadButton = null;
        }

        private void Core_RenderFrame(object sender, EventArgs e)
        {
            // A background compiler never touches VVS or native hooks. Consume its
            // completion exactly once here, on the normal Decal render thread.
            FinishTemplateBuildIfReady();
            FinishMappingUpdateWorkers();
            FinishThemeWorkers();
            if (DateTime.UtcNow >= nextThemeRetryUtc && !updateShuttingDown)
            {
                nextThemeRetryUtc = DateTime.MaxValue;
                BeginThemeCatalogCheck();
            }
            if (notebookRebuildRequested && view != null)
                RebuildMainNotebook();
            ApplyNotebookResize();
            if (!updateShuttingDown && DateTime.UtcNow >= nextAutomaticMappingRetryUtc)
            {
                nextAutomaticMappingRetryUtc = DateTime.MaxValue;
                BeginMappingUpdateCheck();
            }
            PollTemplateFileChanges();
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


        // This control is absent from the UI until a newer official mapping
        // release has been confirmed. Reflection avoids a version-specific VVS
        // Visible/Enabled setter, which differs between legacy VVS releases.
        private static void TrySetVvsVisibility(object control, bool visible)
        {
            if (control == null) return;
            try
            {
                var property = control.GetType().GetProperty("Visible");
                if (property != null && property.CanWrite && property.PropertyType == typeof(bool))
                    property.SetValue(control, visible, null);
                var enabled = control.GetType().GetProperty("Enabled");
                if (enabled != null && enabled.CanWrite && enabled.PropertyType == typeof(bool))
                    enabled.SetValue(control, visible, null);
            }
            catch { /* Different VVS skins expose different presentation controls. */ }
        }

        private static void AppendMappingUpdateLog(string message)
        {
            try
            {
                lock (mappingUpdateLogLock)
                {
                    Directory.CreateDirectory(System.IO.Path.GetDirectoryName(MappingUpdateLogPath));
                    File.AppendAllText(MappingUpdateLogPath,
                        DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + " " + message + Environment.NewLine);
                }
            }
            catch { /* Logging must never interfere with gameplay or update checks. */ }
        }

        private void BeginMappingUpdateCheck(bool manual = false)
        {
            if (updateShuttingDown) return;
            lock (mappingUpdateWorkerLock)
            {
                if (mappingCheckRunning)
                {
                    if (manual) SetStatus("Mapping update check is already running...");
                    return;
                }
                if (!manual && DateTime.UtcNow - lastMappingCheckUtc < TimeSpan.FromSeconds(10))
                    return;
                mappingCheckRunning = true;
                manualMappingCheckPending = manual;
                lastMappingCheckUtc = DateTime.UtcNow;
            }
            AppendMappingUpdateLog("Checking GitHub mappings/latest.json (" +
                (manual ? "manual" : "automatic") + ")");
            if (manual) SetStatus("Checking GitHub for mapping updates...");
            ThreadPool.QueueUserWorkItem(delegate(object unused)
            {
                MappingRelease remote = null;
                string error = null;
                try
                {
                    remote = MappingUpdateService.GetLatest();
                    AppendMappingUpdateLog("Manifest OK, mapping version " + remote.MappingVersion);
                }
                catch (Exception ex)
                {
                    error = ex.Message;
                    AppendMappingUpdateLog("Manifest check FAILED: " + ex);
                }
                lock (mappingUpdateWorkerLock)
                {
                    if (updateShuttingDown) return;
                    checkedMappingRelease = remote;
                    mappingCheckError = error;
                    mappingCheckResultPending = true;
                    mappingCheckRunning = false;
                }
            });
        }

        private void FinishMappingUpdateWorkers()
        {
            MappingRelease remote = null;
            string checkError = null;
            string folder = null;
            string downloadError = null;
            MappingRelease completedDownloadRelease = null;
            bool checkedNow = false, downloadedNow = false, manualCheck = false;
            lock (mappingUpdateWorkerLock)
            {
                if (mappingCheckResultPending)
                {
                    checkedNow = true;
                    remote = checkedMappingRelease;
                    checkError = mappingCheckError;
                    manualCheck = manualMappingCheckPending;
                    manualMappingCheckPending = false;
                    mappingCheckResultPending = false;
                }
                if (mappingDownloadResultPending)
                {
                    downloadedNow = true;
                    folder = mappingDownloadFolderResult;
                    downloadError = mappingDownloadError;
                    completedDownloadRelease = mappingDownloadReleaseResult;
                    mappingDownloadReleaseResult = null;
                    mappingDownloadResultPending = false;
                }
            }
            if (checkedNow)
            {
                // Network failures never prevent the plugin from operating.
                if (remote != null)
                {
                    automaticMappingFailures = 0;
                    nextAutomaticMappingRetryUtc = DateTime.MaxValue;
                    if (availableMappingRelease == null ||
                        !String.Equals(availableMappingRelease.MappingVersion, remote.MappingVersion,
                            StringComparison.Ordinal))
                    {
                        updateFeedback = String.Empty;
                    }
                    availableMappingRelease = remote;
                    downloadedReleaseFolder = MappingUpdateService.FindVerifiedDownload(remote);
                    RefreshMappingUpdateButton();
                    AppendMappingUpdateLog("Installed mapping " + currentMappingReleaseName +
                        "; latest v" + remote.MappingVersion +
                        "; updateAvailable=" + mappingUpdateVisible.ToString());
                    if (manualCheck)
                        SetStatus(mappingUpdateVisible
                            ? "Update v" + remote.MappingVersion + " available. Click Available Update."
                            : "Mappings are current (v" + remote.MappingVersion + ").");
                    else if (mappingUpdateVisible)
                        SetStatus("Mapping update v" + remote.MappingVersion + " available.");
                }
                else
                {
                    // Do NOT discard an earlier successful manifest on a
                    // transient failure. Automatic startup checks are quiet;
                    // manual checks still surface detailed errors for testing.
                    if (availableMappingRelease == null)
                    {
                        mappingUpdateVisible = false;
                        RefreshMappingUpdateButton();
                    }
                    string message = "Mapping update check failed: " +
                        (String.IsNullOrWhiteSpace(checkError) ? "Unknown error" : checkError);
                    ++automaticMappingFailures;
                    int retrySeconds = Math.Min(300, 15 * (1 << Math.Min(automaticMappingFailures, 5)));
                    nextAutomaticMappingRetryUtc = DateTime.UtcNow.AddSeconds(retrySeconds);
                    AppendMappingUpdateLog(message + " (retry in " + retrySeconds + "s)");
                    if (manualCheck)
                    {
                        SetStatus(Shorten(message, 150));
                        try { Host.Actions.AddChatText("AC Customs: " + Shorten(message, 140), 3); }
                        catch { }
                    }
                }
                if (view != null && availableMappingRelease != null) RefreshMappingUpdateWindow();
            }
            if (downloadedNow)
            {
                if (downloadError == null && completedDownloadRelease != null &&
                    !String.IsNullOrWhiteSpace(folder))
                {
                    downloadedReleaseFolder = folder;
                    pendingMappingActivation = completedDownloadRelease;
                    pendingMappingFolder = folder;
                    AppendMappingUpdateLog("Verified v" + completedDownloadRelease.MappingVersion +
                        " at " + folder + "; preparing activation.");
                }
                else
                {
                    string message = "Mapping download failed: " +
                        (String.IsNullOrWhiteSpace(downloadError) ? "Unknown error" : downloadError);
                    AppendMappingUpdateLog(message);
                    SetStatus(Shorten(message, 145));
                    updateFeedback = Shorten(message, 95);
                    if (updateStatusText != null)
                        updateStatusText.Text = Shorten(updateFeedback, 33);
                    if (updateDownloadButton != null)
                    {
                        ChangeButtonArtworkText("UpdateDownload", "Retry Download");
                        TrySetVvsVisibility(updateDownloadButton, true);
                    }
                }
            }

            if (pendingMappingActivation != null)
            {
                if (IsTemplateBuildRunning())
                {
                    updateFeedback = "Downloaded. Waiting for generation.";
                    if (updateStatusText != null)
                        updateStatusText.Text = updateFeedback;
                }
                else
                {
                    MappingRelease release = pendingMappingActivation;
                    string releaseFolder = pendingMappingFolder;
                    pendingMappingActivation = null;
                    pendingMappingFolder = String.Empty;
                    try
                    {
                        ActivateDownloadedMapping(release, releaseFolder);
                    }
                    catch (Exception ex)
                    {
                        AppendMappingUpdateLog("Mapping activation FAILED: " + ex);
                        SetStatus("Mapping activation failed: " + Shorten(ex.Message, 106));
                        updateFeedback = "Install failed: " + ex.Message;
                        if (updateStatusText != null)
                            updateStatusText.Text = Shorten(updateFeedback, 35);
                        ChangeButtonArtworkText("UpdateDownload", "Retry Install");
                    }
                }
            }
        }

        // Called only on the Decal/VVS thread, after a verified download.
        // Switching mappings does not regenerate textures or hot-swap the UI:
        // those are deliberate user actions and may require Restore Default.
        private void ActivateDownloadedMapping(MappingRelease release, string folder)
        {
            if (release == null || String.IsNullOrWhiteSpace(folder))
                throw new InvalidDataException("No verified mapping release is selected.");
            string relative = release.Files["mapping"];
            string newMapping = System.IO.Path.GetFullPath(
                System.IO.Path.Combine(folder, System.IO.Path.GetFileName(relative)));
            if (!File.Exists(newMapping) ||
                !String.Equals(MappingUpdateService.FileHash(newMapping),
                    release.Sha256["mapping"], StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("The downloaded mapping is missing or its checksum changed.");

            TemplateTextureCompiler.ValidateMappingFile(newMapping);
            string settingsFolder = System.IO.Path.GetDirectoryName(MappingSettingsPath);
            Directory.CreateDirectory(settingsFolder);
            string staged = MappingSettingsPath + ".tmp-" + Guid.NewGuid().ToString("N");
            try
            {
                File.WriteAllText(staged, newMapping);
                if (File.Exists(MappingSettingsPath))
                    File.Replace(staged, MappingSettingsPath, MappingSettingsPath + ".bak");
                else
                    File.Move(staged, MappingSettingsPath);
            }
            finally
            {
                if (File.Exists(staged)) File.Delete(staged);
            }

            selectedMappingJson = newMapping;
            downloadedReleaseFolder = folder;
            RefreshMappingUpdateButton();
            RefreshTemplateStatus();
            if (view != null) RefreshMappingUpdateWindow();
            ChangeButtonArtworkText("UpdateDownload", "Installed");
            AppendMappingUpdateLog("Activated official mapping v" + release.MappingVersion +
                "; selected mapping=" + newMapping);
            SetStatus("Mapping v" + release.MappingVersion +
                " installed. Generate Textures, then Apply UI.");
            updateFeedback = "Installed! Generate and Apply.";
            if (updateStatusText != null)
                updateStatusText.Text = updateFeedback;
        }

        private void EnableNativeWindowResize()
        {
            try
            {
                var prop = view.GetType().GetProperty("UserResizeable");
                if (prop != null && prop.CanWrite && prop.PropertyType == typeof(bool))
                    prop.SetValue(view, true, null);
            }
            catch { /* A VVS skin/version without resize support still loads. */ }
        }

        // HudFixedLayout stores the coordinates of its immediate children.
        // Use the same API as the MIT-licensed VVS wrappers: Group.ParentOf +
        // HudFixedLayout.SetControlRect. Tabs can be lazily realized, so failed
        // lookups are retried at the next layout tick.
        private bool TryNotebookControlRect(string name, int x, int y, int w, int h)
        {
            if (notebookLayoutApplied.Contains(name)) return true;
            try
            {
                HudControl control = view[name];
                if (control == null || control.Group == null) return false;
                HudFixedLayout parent = control.Group.ParentOf(control.Name) as HudFixedLayout;
                if (parent == null) return false;
                parent.SetControlRect(control, new Rectangle(x, y, Math.Max(1, w), Math.Max(1, h)));
                notebookLayoutApplied.Add(name);
                return true;
            }
            catch { return false; }
        }

        // Resize the native notebook and useful content areas at ~5 Hz.
        // This is intentionally based on the live VVS client rectangle;
        // VVS persists the user's chosen size separately in vvs.s3db.
        private void ApplyNotebookResize()
        {
            if (view == null || DateTime.UtcNow < nextNotebookLayoutPollUtc) return;
            nextNotebookLayoutPollUtc = DateTime.UtcNow.AddMilliseconds(200);
            Size size;
            try { size = view.ClientArea; }
            catch { return; }
            // Do not allow the user to shrink controls into overlapping rows.
            // Expanding remains unrestricted; VVS saves the chosen size.
            if (size.Width < 430 || size.Height < 326)
            {
                try { view.ClientArea = new Size(Math.Max(430, size.Width), Math.Max(326, size.Height)); }
                catch { }
                return;
            }
            if (size != lastNotebookClientArea)
            {
                lastNotebookClientArea = size;
                notebookLayoutApplied.Clear();
            }
            ApplySharedWindowBackground();
            int dx = size.Width - 430;
            int dy = size.Height - 326;
            int split = dx * 2 / 3;
            TryNotebookControlRect("MainTabs", 0, 0, size.Width, size.Height);

            // Theme tab: preserve the two primary actions' gap and proportions.
            TryNotebookControlRect("BrandTitle", 18, 11, 394 + dx, 24);
            TryNotebookControlRect("TemplatePngText", 101, 47, 172 + dx, 20);
            TryNotebookControlRect("BrowseTemplate", 282 + dx, 40, 130, 29);
            TryNotebookControlRect("TemplateStatusText", 53, 92, 247 + dx, 23);
            TryNotebookControlRect("RefreshTemplate", 303 + dx, 86, 109, 29);
            TryNotebookControlRect("GenerateTemplate", 18, 132, 242 + split, 37);
            TryNotebookControlRect("ApplyGeneratedTemplate", 268 + split, 132, 144 + dx - split, 37);
            TryNotebookControlRect("RestoreVanilla", 18, 178, 394 + dx, 29);
            ResizeButtonArtwork("GenerateTemplate", 242 + split, 37);
            ResizeButtonArtwork("ApplyGeneratedTemplate", 144 + dx - split, 37);
            ResizeButtonArtwork("RestoreVanilla", 394 + dx, 29);
            TryNotebookControlRect("CurrentUiText", 18, 220 + dy, 394 + dx, 21);
            TryNotebookControlRect("StatusText", 18, 245 + dy, 394 + dx, 29);

            // Themes: only the scrollable list expands; bottom actions stay visible.
            TryNotebookControlRect("ThemesHeader", 18, 12, 394 + dx, 25);
            TryNotebookControlRect("ThemesIntro", 18, 40, 394 + dx, 20);
            int themesW = 394 + dx;
            if (TryNotebookControlRect("ThemesList", 18, 66, themesW, 145 + dy))
            {
                try
                {
                    var set = themesList.GetType().GetMethod("SetColumnWidth",
                        new[] { typeof(int), typeof(int) });
                    if (set != null)
                    {
                        set.Invoke(themesList, new object[] { 0, Math.Max(100, themesW - 187) });
                        set.Invoke(themesList, new object[] { 1, 162 });
                    }
                }
                catch { }
            }
            TryNotebookControlRect("ThemesStatus", 18, 215 + dy, 394 + dx, 24);
            TryNotebookControlRect("ThemesRefresh", 18, 248 + dy, 114, 31);
            TryNotebookControlRect("ThemesInstall", 141 + dx / 2, 248 + dy, 132, 31);
            TryNotebookControlRect("ThemesUse", 282 + dx, 248 + dy, 130, 31);

            // Upload page: filename and footer grow with the window.
            TryNotebookControlRect("UploadHeader", 18, 13, 394 + dx, 26);
            TryNotebookControlRect("UploadIntro", 18, 44, 394 + dx, 39);
            TryNotebookControlRect("UploadFilename", 18, 97, 257 + dx, 23);
            TryNotebookControlRect("UploadBrowse", 282 + dx, 90, 130, 31);
            TryNotebookControlRect("UploadGuidelines", 18, 137, 394 + dx, 42);
            TryNotebookControlRect("UploadStatus", 18, 202 + dy, 394 + dx, 39);
            TryNotebookControlRect("UploadSubmit", 18, 249 + dy, 394 + dx, 32);
            ResizeButtonArtwork("UploadSubmit", 394 + dx, 32);


            // Advanced: mapping actions stay aligned with the right margin.
            TryNotebookControlRect("AdvancedHeader", 18, 20, 394 + dx, 24);
            TryNotebookControlRect("AdvancedInfo", 18, 46, 394 + dx, 24);
            TryNotebookControlRect("AdvancedMappingFile", 105, 84, 163 + dx, 20);
            TryNotebookControlRect("AdvancedOptions", 274 + dx, 77, 138, 28);
            TryNotebookControlRect("AdvancedUseDefault", 287 + dx, 120, 125, 28);
            TryNotebookControlRect("AdvancedCheckUpdates", 18, 173, 177, 32);
            TryNotebookControlRect("AdvancedMappingVersion", 18, 219 + dy, 394 + dx, 22);
            TryNotebookControlRect("AdvancedFooter", 18, 246 + dy, 394 + dx, 30);

            // Update: available height goes to the scrollable patch-notes list.
            if (viewHasUpdateTab)
            {
                TryNotebookControlRect("UpdateHeader", 18, 16, 394 + dx, 27);
                TryNotebookControlRect("UpdateInstalled", 18, 48, 394 + dx, 20);
                TryNotebookControlRect("UpdateAvailable", 18, 70, 394 + dx, 20);
                TryNotebookControlRect("UpdateNotesHeader", 18, 95, 393 + dx, 20);
                int noteW = Math.Max(150, 394 + dx);
                if (TryNotebookControlRect("UpdateNotesList", 18, 118, noteW, Math.Max(60, 112 + dy)) &&
                    updateNotesList != null)
                {
                    // VVS fixes list column widths at parse time; resize it too.
                    var set = updateNotesList.GetType().GetMethod("SetColumnWidth",
                        new[] { typeof(int), typeof(int) });
                    if (set != null) try { set.Invoke(updateNotesList,
                        new object[] { 0, Math.Max(100, noteW - 22) }); } catch { }
                }
                TryNotebookControlRect("UpdateInstructions", 18, 238 + dy, 394 + dx, 19);
                TryNotebookControlRect("UpdateDownloadStatus", 18, 259 + dy, 190 + dx, 28);
                TryNotebookControlRect("UpdateOpenFolder", 218 + dx, 262 + dy, 82, 29);
                TryNotebookControlRect("UpdateDownload", 305 + dx, 262 + dy, 107, 29);
            }
        }

        private void RefreshMappingUpdateButton()
        {
            MappingRelease release = availableMappingRelease;
            mappingUpdateVisible = release != null &&
                MappingUpdateService.IsNewer(release.MappingVersion,
                    MappingUpdateService.InstalledVersion(selectedMappingJson, BundledMappingPath, release));
            currentMappingReleaseName = release != null
                ? MappingUpdateService.InstalledVersion(selectedMappingJson, BundledMappingPath, release)
                : "Bundled";
            RefreshAdvancedMappingInfo();
            // A notebook page itself can't be hidden like a PushButton.
            // Reparse only when the update state changes, reusing the SAME
            // HudView identifier so VVS never gets a second bar icon.
            // Do NOT destroy the visible VVS view when an update is installed.
            // Keep the Update page with its success message until next login.
            // A new update can still add the page when it was previously absent.
            if (view != null && mappingUpdateVisible && !viewHasUpdateTab && !rebuildingNotebook)
                notebookRebuildRequested = true;
        }

        private void RefreshAdvancedMappingInfo()
        {
            if (AdvancedMappingFile != null)
                AdvancedMappingFile.Text = UsesBundledMapping() ? "Included mappings"
                    : Shorten(System.IO.Path.GetFileName(selectedMappingJson), 25);
            if (AdvancedMappingVersion != null)
                AdvancedMappingVersion.Text = "Installed mappings: " + currentMappingReleaseName;
        }

        private void RebuildMainNotebook()
        {
            notebookRebuildRequested = false;
            if (view == null || rebuildingNotebook) return;
            rebuildingNotebook = true;
            string previousStatus = status;
            Point oldLocation = Point.Empty;
            Size oldClientArea = Size.Empty;
            bool hasLocation = false;
            bool hasSize = false;
            bool wasVisible = false;
            bool visibilityKnown = false;
            try
            {
                var visibility = view.GetType().GetProperty("Visible");
                if (visibility != null && visibility.CanRead &&
                    visibility.PropertyType == typeof(bool))
                {
                    wasVisible = (bool)visibility.GetValue(view, null);
                    visibilityKnown = true;
                }
            }
            catch { }
            try
            {
                oldLocation = view.Location;
                hasLocation = true;
                oldClientArea = view.ClientArea;
                hasSize = oldClientArea.Width > 0 && oldClientArea.Height > 0;
            }
            catch { /* Some VVS installations persist window geometry automatically. */ }
            try
            {
                DisposeView();
                CreateView();
                if (view != null)
                {
                    if (hasLocation) view.Location = oldLocation;
                    if (hasSize) view.ClientArea = oldClientArea;
                    try
                    {
                        var visibility = view.GetType().GetProperty("Visible");
                        if (visibilityKnown && visibility != null && visibility.CanWrite)
                            visibility.SetValue(view, wasVisible, null);
                    }
                    catch { }
                }
                SetStatus(previousStatus);
            }
            catch (Exception ex)
            {
                AppendMappingUpdateLog("Native notebook rebuild failed: " + ex);
                try { Host.Actions.AddChatText("AC Customs: tabs failed: " + ex.Message, 3); } catch { }
                try { DisposeView(); } catch { }
                // Allow the normal VVS initialization path to retry cleanly.
                nextViewAttemptUtc = DateTime.UtcNow.AddSeconds(2);
            }
            finally { rebuildingNotebook = false; }
        }

        // ------------------------------------ Official Theme Library / Upload
        // The GitHub Contents API lists theme PNG files and their git blob SHA.
        // No theme manifest or version numbers are required.
        private void BeginThemeCatalogCheck(bool manual = false)
        {
            lock (themeWorkerLock)
            {
                if (themeCatalogRunning)
                {
                    if (manual) themesFeedback = "Already checking GitHub...";
                    return;
                }
                if (!manual && DateTime.UtcNow - lastThemeCheckUtc < TimeSpan.FromMinutes(2))
                    return;
                themeCatalogRunning = true;
                lastThemeCheckUtc = DateTime.UtcNow;
            }
            if (manual)
            {
                themesFeedback = "Checking GitHub for themes...";
                RefreshThemesTab();
            }
            ThreadPool.QueueUserWorkItem(delegate(object unused)
            {
                List<GithubThemeItem> list = null;
                string error = null;
                try { list = ThemeLibraryService.GetAvailableThemes(); }
                catch (Exception ex) { error = ex.Message; }
                lock (themeWorkerLock)
                {
                    if (updateShuttingDown) return;
                    themeCatalogResult = list;
                    themeCatalogError = error;
                    themeCatalogPending = true;
                    themeCatalogRunning = false;
                }
            });
        }

        // All VVS operations (including SetStatus) must stay on the render thread.
        private void FinishThemeWorkers()
        {
            List<GithubThemeItem> list = null;
            string catalogError = null, installedPath = null, installError = null;
            string submittedId = null, submitError = null;
            GithubThemeItem installedItem = null;
            bool catalogueDone = false, installDone = false, uploadDone = false;
            lock (themeWorkerLock)
            {
                if (themeCatalogPending)
                {
                    catalogueDone = true;
                    list = themeCatalogResult;
                    catalogError = themeCatalogError;
                    themeCatalogPending = false;
                }
                if (themeInstallPending)
                {
                    installDone = true;
                    installedPath = themeInstallPath;
                    installError = themeInstallError;
                    installedItem = installingTheme;
                    themeInstallPending = false;
                }
                if (uploadPending)
                {
                    uploadDone = true;
                    submittedId = uploadResultId;
                    submitError = uploadResultError;
                    uploadPending = false;
                }
            }
            if (catalogueDone)
            {
                if (catalogError == null)
                {
                    string previous = SelectedOfficialTheme() != null
                        ? SelectedOfficialTheme().Name : null;
                    availableThemes.Clear();
                    availableThemes.AddRange(list);
                    themeCatalogReady = true;
                    selectedOfficialTheme = -1;
                    if (!String.IsNullOrEmpty(previous))
                        for (int i = 0; i < availableThemes.Count; ++i)
                            if (String.Equals(availableThemes[i].Name, previous,
                                StringComparison.OrdinalIgnoreCase)) { selectedOfficialTheme = i; break; }
                    if (selectedOfficialTheme < 0 && availableThemes.Count > 0)
                        selectedOfficialTheme = 0;
                    int updates = CountThemeUpdates();
                    themesFeedback = availableThemes.Count == 0
                        ? "No themes yet. Add PNGs to GitHub themes/."
                        : updates > 0
                        ? updates + " theme update(s) available."
                        : availableThemes.Count + " available theme(s).";
                    if (updates > 0)
                        SetStatus(updates + " community theme update(s) available in Themes.");
                    nextThemeRetryUtc = DateTime.MaxValue;
                }
                else
                {
                    themesFeedback = !themeCatalogReady
                        ? "Could not load GitHub themes. Press Refresh."
                        : "GitHub unavailable. Showing last known themes.";
                    nextThemeRetryUtc = DateTime.UtcNow.AddSeconds(90);
                    AppendMappingUpdateLog("Theme library check failed: " + catalogError);
                }
                RefreshThemesTab();
            }
            if (installDone)
            {
                if (installError != null)
                {
                    themesFeedback = "Install failed: " + Shorten(installError, 80);
                    AppendMappingUpdateLog("Theme install failed: " + installError);
                }
                else
                {
                    try
                    {
                        SelectThemePng(installedPath);
                        themesFeedback = System.IO.Path.GetFileName(installedPath) +
                            " installed. Generate Textures on Theme tab.";
                        SetStatus(themesFeedback);
                        AppendMappingUpdateLog("Official theme installed: " + installedItem.Name);
                    }
                    catch (Exception ex)
                    {
                        themesFeedback = "Theme installed; selection failed: " + ex.Message;
                    }
                }
                RefreshThemesTab();
            }
            if (uploadDone)
            {
                uploadFeedback = submitError == null
                    ? "Submitted for review. Receipt: " + Shorten(submittedId, 20)
                    : "Submission failed: " + Shorten(submitError, 85);
                if (submitError == null) SetStatus("Theme submitted for review.");
                RefreshUploadTab();
            }
        }

        private int CountThemeUpdates()
        {
            int count = 0;
            foreach (GithubThemeItem item in availableThemes)
                if (ThemeLibraryService.State(item) == "Update available") ++count;
            return count;
        }

        private GithubThemeItem SelectedOfficialTheme()
        {
            return selectedOfficialTheme >= 0 && selectedOfficialTheme < availableThemes.Count
                ? availableThemes[selectedOfficialTheme] : null;
        }

        private void RefreshThemesTab()
        {
            if (themesList == null) return;
            while (themesList.RowCount > 0)
                themesList.RemoveRow(themesList.RowCount - 1);
            foreach (GithubThemeItem item in availableThemes)
            {
                HudList.HudListRowAccessor row = themesList.AddRow();
                HudStaticText label = (HudStaticText)row[0];
                HudStaticText state = (HudStaticText)row[1];
                label.Text = Shorten(System.IO.Path.GetFileNameWithoutExtension(item.Name), 26);
                state.Text = ThemeLibraryService.State(item);
                TrySetColor(label, UiBodyText);
                TrySetColor(state, state.Text == "Update available" ? UiGold :
                    state.Text == "Installed" ? UiGreen : UiMuted);
            }
            RefreshThemeActions();
        }

        private void RefreshThemeActions()
        {
            GithubThemeItem selected = SelectedOfficialTheme();
            if (themesStatus != null)
            {
                themesStatus.Text = Shorten(themesFeedback, 69);
                TrySetColor(themesStatus, CountThemeUpdates() > 0 ? UiGold : UiBodyText);
            }
            bool busy;
            lock (themeWorkerLock) busy = themeInstallRunning;
            ChangeButtonArtworkText("ThemesInstall", busy ? "Downloading..." :
                selected == null ? "Install/Update" :
                ThemeLibraryService.State(selected) == "Not installed" ? "Install" :
                ThemeLibraryService.State(selected) == "Update available" ? "Update" : "Installed");
            TrySetVvsVisibility(themesInstall,
                selected != null && !busy && ThemeLibraryService.State(selected) != "Installed");
            TrySetVvsVisibility(themesUse,
                selected != null && !busy && ThemeLibraryService.InstalledSha(selected) != null);
        }

        private void ThemesList_Click(object sender, int row, int col)
        {
            if (row < 0 || row >= availableThemes.Count) return;
            selectedOfficialTheme = row;
            GithubThemeItem item = SelectedOfficialTheme();
            themesFeedback = System.IO.Path.GetFileNameWithoutExtension(item.Name) + ": " +
                ThemeLibraryService.State(item);
            RefreshThemeActions();
        }

        private void ThemesRefresh_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
                BeginThemeCatalogCheck(true);
        }

        private void ThemesInstall_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType != ControlMouseEventArgs.MouseEventType.MouseHit) return;
            GithubThemeItem item = SelectedOfficialTheme();
            if (item == null) return;
            if (IsTemplateBuildRunning())
            {
                themesFeedback = "Wait for texture generation to finish.";
                RefreshThemesTab();
                return;
            }
            lock (themeWorkerLock)
            {
                if (themeInstallRunning || themeInstallPending ||
                    ThemeLibraryService.State(item) == "Installed") return;
                themeInstallRunning = true;
                installingTheme = item;
            }
            themesFeedback = "Downloading and checking " + item.Name + "...";
            RefreshThemesTab();
            ThreadPool.QueueUserWorkItem(delegate(object unused)
            {
                string path = null, error = null;
                try { path = ThemeLibraryService.Install(item); }
                catch (Exception ex) { error = ex.Message; }
                lock (themeWorkerLock)
                {
                    if (updateShuttingDown) return;
                    themeInstallPath = path;
                    themeInstallError = error;
                    themeInstallPending = true;
                    themeInstallRunning = false;
                }
            });
        }

        private void ThemesUse_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType != ControlMouseEventArgs.MouseEventType.MouseHit) return;
            GithubThemeItem item = SelectedOfficialTheme();
            if (item == null || ThemeLibraryService.InstalledSha(item) == null) return;
            if (IsTemplateBuildRunning())
            {
                themesFeedback = "Wait for texture generation to finish.";
                RefreshThemesTab();
                return;
            }
            try
            {
                string file = ThemeLibraryService.ThemePath(item);
                ThemeLibraryService.ValidatePng(file);
                SelectThemePng(file);
                themesFeedback = item.Name + " selected. Generate Textures on Theme tab.";
                RefreshThemesTab();
                SetStatus(themesFeedback);
            }
            catch (Exception ex)
            {
                themesFeedback = "Could not select theme: " + ex.Message;
                RefreshThemesTab();
            }
        }

        private void SelectThemePng(string file)
        {
            selectedTemplatePng = System.IO.Path.GetFullPath(file);
            Directory.CreateDirectory(System.IO.Path.GetDirectoryName(TemplateSourceSettingsPath));
            File.WriteAllText(TemplateSourceSettingsPath, selectedTemplatePng);
            RefreshTemplateStatus();
        }

        private void RefreshUploadTab()
        {
            if (uploadFilename != null)
                uploadFilename.Text = String.IsNullOrEmpty(selectedUploadPng)
                    ? "No PNG selected"
                    : Shorten(System.IO.Path.GetFileName(selectedUploadPng), 35);
            if (uploadStatus != null)
            {
                uploadStatus.Text = Shorten(uploadFeedback, 100);
                TrySetColor(uploadStatus, UiBodyText);
            }
            bool busy;
            lock (themeWorkerLock) busy = uploadRunning;
            ChangeButtonArtworkText("UploadSubmit", busy ? "Uploading..." : "Submit for Review");
            TrySetVvsVisibility(uploadSubmit, !busy && !String.IsNullOrEmpty(selectedUploadPng));
        }

        private void UploadBrowse_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType != ControlMouseEventArgs.MouseEventType.MouseHit) return;
            try
            {
                using (var dialog = new System.Windows.Forms.OpenFileDialog())
                {
                    dialog.Filter = "Theme PNG (*.png)|*.png";
                    dialog.Title = "Choose 2000 x 2000 Theme PNG to Share";
                    if (!String.IsNullOrEmpty(selectedTemplatePng) && File.Exists(selectedTemplatePng))
                        dialog.FileName = selectedTemplatePng;
                    if (dialog.ShowDialog() != System.Windows.Forms.DialogResult.OK) return;
                    ThemeLibraryService.ValidatePng(dialog.FileName);
                    if (!ThemeLibraryService.IsSafeName(System.IO.Path.GetFileName(dialog.FileName)))
                        throw new InvalidDataException("Use a simple PNG filename (letters, numbers, spaces). ");
                    selectedUploadPng = System.IO.Path.GetFullPath(dialog.FileName);
                    uploadFeedback = "Validated 2000 x 2000 PNG. Ready for submission.";
                }
            }
            catch (Exception ex)
            {
                selectedUploadPng = String.Empty;
                uploadFeedback = "Invalid file: " + ex.Message;
            }
            RefreshUploadTab();
        }

        private void UploadSubmit_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType != ControlMouseEventArgs.MouseEventType.MouseHit ||
                String.IsNullOrWhiteSpace(selectedUploadPng)) return;
            lock (themeWorkerLock)
            {
                if (uploadRunning || uploadPending) return;
                uploadRunning = true;
            }
            string source = selectedUploadPng;
            uploadFeedback = "Submitting to AC Customs website...";
            RefreshUploadTab();
            ThreadPool.QueueUserWorkItem(delegate(object unused)
            {
                string receipt = null, error = null;
                try { receipt = ThemeLibraryService.SubmitTheme(source); }
                catch (Exception ex) { error = ex.Message; }
                lock (themeWorkerLock)
                {
                    if (updateShuttingDown) return;
                    uploadResultId = receipt;
                    uploadResultError = error;
                    uploadPending = true;
                    uploadRunning = false;
                }
            });
        }

        private void AdvancedCheckUpdates_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
                BeginMappingUpdateCheck(true);
        }

        // The native VVS List renders the full changelog with a scrollbar.
        // VVS examples use HudList.AddRow() + HudStaticText for text columns.
        private List<string> GetUpdateNoteLines()
        {
            var lines = new List<string>();
            if (availableMappingRelease == null || availableMappingRelease.Changelog == null)
                return lines;
            foreach (string note in availableMappingRelease.Changelog)
            {
                string remaining = (note ?? String.Empty).Replace('\r', ' ').Replace('\n', ' ').Trim();
                bool first = true;
                do
                {
                    int take = Math.Min(54, remaining.Length);
                    if (take < remaining.Length)
                    {
                        int space = remaining.LastIndexOf(' ', take, take);
                        if (space >= 12) take = space;
                    }
                    string chunk = remaining.Substring(0, take).Trim();
                    lines.Add((first ? "- " : "  ") + chunk);
                    remaining = remaining.Substring(take).TrimStart();
                    first = false;
                } while (remaining.Length != 0);
            }
            return lines;
        }

        private void RefreshMappingUpdateWindow()
        {
            if (view == null || !viewHasUpdateTab || availableMappingRelease == null || updateNotesList == null)
                return;
            currentMappingReleaseName = MappingUpdateService.InstalledVersion(
                selectedMappingJson, BundledMappingPath, availableMappingRelease);
            updateInstalledText.Text = "Current mappings: " + currentMappingReleaseName;
            updateAvailableText.Text = "Official release: v" + availableMappingRelease.MappingVersion;
            while (updateNotesList.RowCount > 0)
                updateNotesList.RemoveRow(updateNotesList.RowCount - 1);
            foreach (string line in GetUpdateNoteLines())
            {
                HudList.HudListRowAccessor row = updateNotesList.AddRow();
                HudStaticText caption = (HudStaticText)row[0];
                caption.Text = line;
                TrySetColor(caption, UiBodyText);
            }
            bool supported = MappingUpdateService.PluginSupported(availableMappingRelease);
            bool downloading;
            lock (mappingUpdateWorkerLock) downloading = mappingDownloadRunning;
            bool installed = !MappingUpdateService.IsNewer(
                availableMappingRelease.MappingVersion, currentMappingReleaseName);
            UpdateHeader.Text = installed ? "MAPPING INSTALLED" : "MAPPING UPDATE AVAILABLE";
            TrySetColor(UpdateHeader, installed ? UiGreen : UiGold);
            ChangeButtonArtworkText("UpdateDownload", !supported ? "Plugin Too Old" :
                installed ? "Installed" : downloading ? "Downloading..." : "Install Update");
            if (String.IsNullOrWhiteSpace(updateFeedback))
                updateStatusText.Text = !supported ? "Requires plugin v" +
                    availableMappingRelease.MinimumPluginVersion :
                    installed ? "Installed. Generate and Apply." :
                    downloading ? "Downloading and verifying..." :
                    "Generate and Apply after install.";
            else updateStatusText.Text = Shorten(updateFeedback, 36);
            TrySetColor(updateStatusText, installed ? UiGreen : UiMuted);
            TrySetVvsVisibility(updateOpenFolderButton,
                !String.IsNullOrEmpty(downloadedReleaseFolder) &&
                Directory.Exists(downloadedReleaseFolder));
            TrySetVvsVisibility(updateDownloadButton, supported && !installed && !downloading);
        }

        private void UpdateOpenFolder_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType != ControlMouseEventArgs.MouseEventType.MouseHit ||
                String.IsNullOrEmpty(downloadedReleaseFolder)) return;
            try
            {
                if (Directory.Exists(downloadedReleaseFolder))
                    Process.Start("explorer.exe", "\"" + downloadedReleaseFolder + "\"");
            }
            catch (Exception ex)
            {
                updateFeedback = "Could not open folder: " + ex.Message;
                if (updateStatusText != null)
                    updateStatusText.Text = Shorten(updateFeedback, 36);
            }
        }

        private void UpdateDownload_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType != ControlMouseEventArgs.MouseEventType.MouseHit ||
                availableMappingRelease == null ||
                !MappingUpdateService.PluginSupported(availableMappingRelease)) return;
            if (IsTemplateBuildRunning())
            {
                updateFeedback = "Wait for texture generation.";
                updateStatusText.Text = updateFeedback;
                return;
            }
            if (!MappingUpdateService.IsNewer(availableMappingRelease.MappingVersion,
                MappingUpdateService.InstalledVersion(selectedMappingJson, BundledMappingPath,
                    availableMappingRelease))) return;
            MappingRelease release = availableMappingRelease;
            lock (mappingUpdateWorkerLock)
            {
                if (mappingDownloadRunning) return;
                mappingDownloadRunning = true;
            }
            updateFeedback = String.Empty;
            updateStatusText.Text = "Downloading and verifying...";
            ChangeButtonArtworkText("UpdateDownload", "Downloading...");
            TrySetVvsVisibility(updateDownloadButton, false);
            ThreadPool.QueueUserWorkItem(delegate(object unused)
            {
                string folder = null, error = null;
                try { folder = MappingUpdateService.DownloadRelease(release); }
                catch (Exception ex) { error = ex.Message; }
                lock (mappingUpdateWorkerLock)
                {
                    if (updateShuttingDown) return;
                    mappingDownloadFolderResult = folder;
                    mappingDownloadError = error;
                    mappingDownloadReleaseResult = release;
                    mappingDownloadResultPending = true;
                    mappingDownloadRunning = false;
                }
            });
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

        private void BrowseTemplate_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
            {
                BrowseTemplate_Click();
            }
        }

        private void AdvancedOptions_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType != ControlMouseEventArgs.MouseEventType.MouseHit) return;
            BrowseMapping_Click();
        }

        private void AdvancedUseDefault_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
                RestoreBundledMapping();
        }

        private void RefreshTemplate_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
            {
                RefreshTemplateStatus();
            }
        }

        private void GenerateTemplate_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
                GenerateTemplate_Click();
        }

        private void ApplyGeneratedTemplate_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
                ApplyGeneratedTemplate_Click();
        }

        private bool IsTemplateBuildRunning()
        {
            lock (templateWorkerLock) return templateBuildRunning;
        }

        private static string Shorten(string value, int maxLength)
        {
            if (String.IsNullOrEmpty(value)) return String.Empty;
            return value.Length <= maxLength ? value : value.Substring(0, maxLength - 3) + "...";
        }

        private static string ShortenPath(string path, int maxLength)
        {
            if (String.IsNullOrEmpty(path) || path.Length <= maxLength) return path;
            // Keep the basename visible even when the theme lives deep under AppData.
            return path.Substring(0, 10) + "..." + path.Substring(path.Length - (maxLength - 13));
        }

        private static string QuickFileStamp(string path)
        {
            try
            {
                if (String.IsNullOrWhiteSpace(path) || !File.Exists(path)) return String.Empty;
                var info = new FileInfo(path);
                return info.Length.ToString() + ":" + info.LastWriteTimeUtc.Ticks.ToString();
            }
            catch (IOException) { return String.Empty; }
            catch (UnauthorizedAccessException) { return String.Empty; }
        }

        private void PollTemplateFileChanges()
        {
            if (TemplateStatusText == null || IsTemplateBuildRunning() ||
                DateTime.UtcNow < nextTemplatePollUtc) return;
            nextTemplatePollUtc = DateTime.UtcNow.AddSeconds(3);
            string png = QuickFileStamp(selectedTemplatePng);
            string mapping = QuickFileStamp(selectedMappingJson);
            if (png == observedPngStamp && mapping == observedMappingStamp) return;
            observedPngStamp = png;
            observedMappingStamp = mapping;
            if (png.Length == 0 || mapping.Length == 0)
                RefreshTemplateStatus();
            else
                SetTemplateVisual("\u21bb", "NEEDS GENERATION",
                    "Input file changed. Reload Status or Generate Textures.",
                    Color.FromArgb(247, 186, 74));
        }

        private void RefreshTemplateStatus()
        {
            if (TemplatePngText == null) return;
            observedPngStamp = QuickFileStamp(selectedTemplatePng);
            observedMappingStamp = QuickFileStamp(selectedMappingJson);
            if (TemplateLabel != null)
                TemplateLabel.Text = "THEME";
            TemplatePngText.Text = String.IsNullOrWhiteSpace(selectedTemplatePng)
                ? "Choose theme..."
                : Shorten(System.IO.Path.GetFileName(selectedTemplatePng), 22);
            RefreshAdvancedMappingInfo();
            Color amber = Color.FromArgb(247, 186, 74);
            Color red = Color.FromArgb(232, 98, 103);
            Color green = Color.FromArgb(82, 210, 133);
            if (IsTemplateBuildRunning())
            {
                SetTemplateVisual("...", "GENERATING...", "Converting every mapped DID in the background.", amber);
                return;
            }
            if (!File.Exists(selectedMappingJson))
            {
                SetTemplateVisual("X", "MAPPING MISSING", "Reinstall AC Customs or select mapping in Advanced.", red);
                return;
            }
            if (String.IsNullOrWhiteSpace(selectedTemplatePng))
            {
                SetTemplateVisual("!", "SELECT THEME", "Choose a theme image to begin.", amber);
                return;
            }
            if (!File.Exists(selectedTemplatePng))
            {
                SetTemplateVisual("X", "THEME FILE MISSING", "Choose an existing theme image.", red);
                return;
            }
            string detail;
            TemplateBuildState state = TemplateTextureCompiler.CheckStatusAll(
                selectedTemplatePng, selectedMappingJson, GeneratedTemplateDirectory, out detail);
            switch (state)
            {
                case TemplateBuildState.UpToDate:
                    SetTemplateVisual("\u2713", "UP TO DATE", detail, green);
                    break;
                case TemplateBuildState.NotGenerated:
                    SetTemplateVisual("+", "NOT GENERATED", "No textures generated yet. Click Generate Textures.", amber);
                    break;
                case TemplateBuildState.RegenerationRequired:
                    SetTemplateVisual("\u21bb", "NEEDS GENERATION", "Theme or mapping changed. Generate textures.", amber);
                    break;
                default:
                    SetTemplateVisual("X", "CHECK FILES", detail, red);
                    break;
            }
        }

        private void BrowseTemplate_Click()
        {
            try
            {
                if (IsTemplateBuildRunning())
                {
                    SetStatus("Wait until generation finishes before changing themes.");
                    return;
                }
                using (var dialog = new System.Windows.Forms.OpenFileDialog())
                {
                    dialog.Filter = "Theme Images (*.png)|*.png";
                    dialog.Title = "Choose Theme Image (2000 x 2000)";
                    dialog.CheckFileExists = true;
                    dialog.Multiselect = false;
                    if (!String.IsNullOrWhiteSpace(selectedTemplatePng) && File.Exists(selectedTemplatePng))
                        dialog.FileName = selectedTemplatePng;
                    if (dialog.ShowDialog() != System.Windows.Forms.DialogResult.OK)
                        return;
                    selectedTemplatePng = System.IO.Path.GetFullPath(dialog.FileName);
                    Directory.CreateDirectory(System.IO.Path.GetDirectoryName(TemplateSourceSettingsPath));
                    File.WriteAllText(TemplateSourceSettingsPath, selectedTemplatePng);
                    RefreshTemplateStatus();
                    SetStatus("Theme selected: " + System.IO.Path.GetFileName(selectedTemplatePng));
                }
            }
            catch (Exception ex) { SetStatus("Theme selection failed: " + ex.Message); }
        }

        private bool UsesBundledMapping()
        {
            return String.Equals(selectedMappingJson, BundledMappingPath,
                StringComparison.OrdinalIgnoreCase);
        }

        private void RestoreBundledMapping()
        {
            try
            {
                if (IsTemplateBuildRunning())
                {
                    SetStatus("Wait until generation finishes before changing mappings.");
                    return;
                }
                string mappingError;
                if (!EnsureBundledMapping(out mappingError))
                {
                    SetStatus("Default mapping error: " + mappingError);
                    RefreshTemplateStatus();
                    return;
                }
                // No user-configured override is needed to use bundled data.
                if (File.Exists(MappingSettingsPath)) File.Delete(MappingSettingsPath);
                selectedMappingJson = BundledMappingPath;
                RefreshTemplateStatus();
                RefreshMappingUpdateButton();
                SetStatus("Default mapping restored.");
            }
            catch (Exception ex) { SetStatus("Mapping reset failed: " + ex.Message); }
        }

        private void BrowseMapping_Click()
        {
            try
            {
                if (IsTemplateBuildRunning())
                {
                    SetStatus("Wait for generation before selecting a new mapping.");
                    return;
                }
                using (var dialog = new System.Windows.Forms.OpenFileDialog())
                {
                    dialog.Filter = "AC Customs Mapping (*.json)|*.json|All Files (*.*)|*.*";
                    dialog.Title = "Load Texture Mapping JSON";
                    dialog.CheckFileExists = true;
                    dialog.Multiselect = false;
                    if (!String.IsNullOrWhiteSpace(selectedMappingJson) && File.Exists(selectedMappingJson))
                        dialog.FileName = selectedMappingJson;
                    if (dialog.ShowDialog() != System.Windows.Forms.DialogResult.OK) return;
                    selectedMappingJson = System.IO.Path.GetFullPath(dialog.FileName);
                    Directory.CreateDirectory(System.IO.Path.GetDirectoryName(MappingSettingsPath));
                    File.WriteAllText(MappingSettingsPath, selectedMappingJson);
                    RefreshTemplateStatus();
                    RefreshMappingUpdateButton();
                    SetStatus("Custom mapping selected. Generate textures to update.");
                }
            }
            catch (Exception ex) { SetStatus("Mapping selection failed: " + ex.Message); }
        }

        private void GenerateTemplate_Click()
        {
            try
            {
                if (String.IsNullOrWhiteSpace(selectedTemplatePng) || !File.Exists(selectedTemplatePng))
                {
                    SetStatus("Choose a theme file first.");
                    RefreshTemplateStatus();
                    return;
                }
                if (String.IsNullOrWhiteSpace(selectedMappingJson) || !File.Exists(selectedMappingJson))
                {
                    SetStatus("Mapping data missing. Reinstall AC Customs or use Advanced.");
                    RefreshTemplateStatus();
                    return;
                }
                if (String.Equals(activePackInstallId, GeneratedTemplateInstallId, StringComparison.Ordinal) &&
                    nativeReady && NativeBridge.GetActiveMode() == 1)
                {
                    SetStatus("Restore Default before regenerating the active theme.");
                    return;
                }
                lock (templateWorkerLock)
                {
                    if (templateBuildRunning)
                    {
                        SetStatus("Texture generation is already running.");
                        return;
                    }
                    templateBuildRunning = true;
                    completedTemplateReport = null;
                    completedTemplateError = null;
                    templateBuildResultPending = false;
                }
                string source = selectedTemplatePng;
                string mappingSource = selectedMappingJson;
                RefreshTemplateStatus();
                SetStatus("Generating textures...");
                ThreadPool.QueueUserWorkItem(delegate(object ignored)
                {
                    TemplateBuildReport report = null;
                    string error = null;
                    try
                    {
                        report = TemplateTextureCompiler.GenerateAll(
                            source, mappingSource, GeneratedTemplateDirectory);
                    }
                    catch (Exception ex) { error = ex.Message; }
                    lock (templateWorkerLock)
                    {
                        completedTemplateReport = report;
                        completedTemplateError = error;
                        templateBuildResultPending = true;
                    }
                });
            }
            catch (Exception ex)
            {
                lock (templateWorkerLock) templateBuildRunning = false;
                SetStatus("Generate failed: " + ex.Message);
                RefreshTemplateStatus();
            }
        }

        private void FinishTemplateBuildIfReady()
        {
            if (!templateBuildResultPending) return;
            TemplateBuildReport report;
            string error;
            lock (templateWorkerLock)
            {
                if (!templateBuildResultPending) return;
                report = completedTemplateReport;
                error = completedTemplateError;
                templateBuildResultPending = false;
                templateBuildRunning = false;
            }
            try
            {
                RefreshTemplateStatus();
                if (error != null)
                {
                    SetTemplateVisual("X", "GENERATION FAILED", error,
                        Color.FromArgb(232, 98, 103));
                    SetStatus("Texture generation failed: " + error);
                    return;
                }
                RefreshInstalledPacks(report != null && report.HasReplacementTextures
                    ? GeneratedTemplateInstallId : PackManager.LoadLastSelectedInstallId());
                if (report == null)
                {
                    SetStatus("Generation did not return a result.");
                    return;
                }
                SetStatus("Textures ready. Click Apply UI.");
            }
            catch (Exception ex) { SetStatus("Generation completion error: " + ex.Message); }
        }

        private bool CheckGeneratedTemplateReady()
        {
            if (IsTemplateBuildRunning())
            {
                SetStatus("Wait for texture generation to finish.");
                return false;
            }
            if (String.IsNullOrWhiteSpace(selectedTemplatePng) || !File.Exists(selectedTemplatePng) ||
                String.IsNullOrWhiteSpace(selectedMappingJson) || !File.Exists(selectedMappingJson))
            {
                RefreshTemplateStatus();
                SetStatus("Theme or mapping file missing. Check your files.");
                return false;
            }
            string detail;
            TemplateBuildState state = TemplateTextureCompiler.CheckStatusAll(
                selectedTemplatePng, selectedMappingJson,
                GeneratedTemplateDirectory, out detail);
            if (state != TemplateBuildState.UpToDate)
            {
                RefreshTemplateStatus();
                SetStatus(state == TemplateBuildState.RegenerationRequired
                    ? "Theme or mapping changed. Click Generate Textures."
                    : state == TemplateBuildState.NotGenerated
                    ? "Generate textures before applying this theme."
                    : "Theme is not ready. Check both selected files.");
                return false;
            }
            if (!File.Exists(System.IO.Path.Combine(GeneratedTemplateDirectory, "manifest.json")))
            {
                SetStatus("Generated theme files are incomplete. Regenerate textures.");
                return false;
            }
            return true;
        }

        private void ApplyGeneratedTemplate_Click()
        {
            try
            {
                if (!CheckGeneratedTemplateReady()) return;
                // Native theme application is shared with the deprecated ACUI path, including
                // vanilla restore before switching, and the native Apply cooldown.
                RefreshInstalledPacks(GeneratedTemplateInstallId);
                InstalledPack generated = installedPacks.Find(p =>
                    String.Equals(p.InstallId, GeneratedTemplateInstallId, StringComparison.Ordinal));
                if (generated == null)
                {
                    SetStatus("Generated pack is missing or invalid. Regenerate textures.");
                    return;
                }
                ApplyInstalledPack(generated);
            }
            catch (Exception ex) { SetStatus("Apply theme error: " + ex.Message); }
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
            activeThemeDisplayName = String.Empty;
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
            // DEPRECATED ACUI IMPORT. Hidden from the VVS interface, preserved
            // for backward compatibility; remove later after PNG rollout.
            try
            {
                if (IsTemplateBuildRunning())
                {
                    SetStatus("Wait for texture generation to finish before importing a pack.");
                    return;
                }
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

        // DEPRECATED ACUI PACK SELECT/APPLY UI. No control wires this up now.
        // Keep implementation for rollback and later removal.
        private void ApplyTheme_Click(object sender, EventArgs e)
        {
            try
            {
                InstalledPack selected = GetSelectedPack();
                if (selected == null)
                {
                    SetStatus("No theme selected. Generate textures first.");
                    return;
                }
                ApplyInstalledPack(selected);
            }
            catch (Exception ex) { SetStatus("Apply error: " + ex.Message); }
        }

        private void ApplyInstalledPack(InstalledPack selected)
        {
            if (!nativeReady)
            {
                SetStatus("Native engine is not ready");
                return;
            }
            if (IsTemplateBuildRunning())
            {
                SetStatus("Wait for texture generation to finish before applying a theme.");
                return;
            }
            if (String.Equals(selected.InstallId, GeneratedTemplateInstallId, StringComparison.Ordinal)
                && !CheckGeneratedTemplateReady())
                return;

            if (String.Equals(activePackInstallId, selected.InstallId, StringComparison.Ordinal) &&
                NativeBridge.GetActiveMode() == 1)
            {
                UpdateCurrentUiText();
                SetStatus("Theme is already active.");
                return;
            }
            if (!BeginChange()) return;

            // Theme A -> Theme B is intentionally Vanilla -> B. DIDs only in
            // theme A must be restored before loading theme B.
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
            SetStatus("Applying theme...");
            bool ok = NativeBridge.ApplyReplacement();
            string selectedDisplayName =
                String.Equals(selected.InstallId, GeneratedTemplateInstallId,
                    StringComparison.Ordinal) &&
                !String.IsNullOrWhiteSpace(selectedTemplatePng)
                    ? System.IO.Path.GetFileNameWithoutExtension(selectedTemplatePng)
                    : selected.Name;
            if (NativeBridge.GetActiveMode() == 1)
            {
                activePackInstallId = selected.InstallId;
                activeThemeDisplayName = selectedDisplayName;
            }
            UpdateCurrentUiText();
            if (!ok)
            {
                SetStatus("Apply incomplete - restore AC Default or restart AC");
                return;
            }
            activePackInstallId = selected.InstallId;
            activeThemeDisplayName = selectedDisplayName;
            PackManager.SaveLastSelectedInstallId(selected.InstallId);
            UpdateCurrentUiText();
            SetStatus("Theme applied.");
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
                    ? "Default theme restored."
                    : "Restore incomplete - close and restart AC if problems continue.");
            }
            catch (Exception ex)
            {
                SetStatus("Restore error: " + ex.Message);
            }
        }

    }
}
