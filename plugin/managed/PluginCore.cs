using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Threading;
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
        // The official mapping is EMBEDDED in ACCustoms.dll. On startup we
        // extract it to app data so the compiler can read a normal JSON path.
        // Shipping a new mapping requires only a plugin release, not user setup.
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
        private bool advancedMode;
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
            using (Stream stream = typeof(PluginCore).Assembly.GetManifestResourceStream("ACCustoms.mainView.xml"))
            using (StreamReader reader = new StreamReader(stream))
                new VirindiViewService.XMLParsers.Decal3XMLParser().Parse(reader.ReadToEnd(), out properties, out controls);

            view = new HudView(properties, controls, "ACCustoms.Main");
            view.ClientArea = new Size(430, 326);
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
            RefreshTemplate = (HudButton)view["RefreshTemplate"];
            GenerateTemplate = (HudButton)view["GenerateTemplate"];
            ApplyGeneratedTemplate = (HudButton)view["ApplyGeneratedTemplate"];
            RestoreVanilla = (HudButton)view["RestoreVanilla"];

            // DEPRECATED ACUI UI: hidden in favor of the theme + mapping workflow.
            // ImportPack, ThemeChoice, ApplyTheme handlers and PackManager remain
            // below for backward compatibility; remove in a future release.
            ThemeChoice = null;
            ApplyTheme = null;
            ImportPack = null;

            BrowseTemplate.MouseEvent += BrowseTemplate_MouseEvent;
            AdvancedOptions.MouseEvent += AdvancedOptions_MouseEvent;
            RefreshTemplate.MouseEvent += RefreshTemplate_MouseEvent;
            GenerateTemplate.MouseEvent += GenerateTemplate_MouseEvent;
            ApplyGeneratedTemplate.MouseEvent += ApplyGeneratedTemplate_MouseEvent;
            RestoreVanilla.MouseEvent += RestoreVanilla_MouseEvent;
            ApplyModernAppearance();
            TryApplyCustomViewIcon();
            RefreshInstalledPacks(null);
            RefreshTemplateStatus();
            UpdateCurrentUiText();
            SetStatus(nativeReady ? BuildReadyStatus() : "Native engine failed to initialize");
        }

        private static void TrySetColor(object target, Color color)
        {
            if (target == null) return;
            // VVS control implementations vary; avoid a hard dependency on
            // presentation-only color properties not present in older builds.
            foreach (string name in new[] { "TextColor", "ForeColor" })
            {
                var property = target.GetType().GetProperty(name);
                if (property != null && property.CanWrite && property.PropertyType == typeof(Color))
                {
                    try { property.SetValue(target, color, null); } catch { }
                    return;
                }
            }
        }

        private void ApplyModernAppearance()
        {
            // Opaque navy content background, compatible with the existing VVS
            // FixedLayout. VVS still controls the outer titlebar/button skins.
            try
            {
                object layout = view.Controls.HeadControl;
                var imageProperty = layout.GetType().GetProperty("Image");
                if (imageProperty != null && imageProperty.CanWrite)
                    imageProperty.SetValue(layout,
                        new ACImage(Color.FromArgb(255, 13, 20, 30)), null);
            }
            catch { /* VVS versions without layout images use their own theme. */ }
            Color gold = Color.FromArgb(223, 174, 85);
            Color blue = Color.FromArgb(100, 168, 222);
            Color muted = Color.FromArgb(175, 192, 212);
            TrySetColor(BrandTitle, gold);
            TrySetColor(BrandSubtitle, blue);
            TrySetColor(TemplateLabel, gold);
            TrySetColor(CurrentUiText, muted);
            TrySetColor(TemplatePngText, muted);
            TrySetColor(StatusText, Color.FromArgb(190, 207, 223));
            // VVS skins vary. Best effort only: some expose a writable Image
            // for buttons; others ignore it and retain the user's VVS skin.
            TrySetButtonSurface(GenerateTemplate, Color.FromArgb(145, 100, 37));
            TrySetButtonSurface(ApplyGeneratedTemplate, Color.FromArgb(39, 89, 142));
            TrySetButtonSurface(RestoreVanilla, Color.FromArgb(29, 43, 58));
            TrySetButtonSurface(AdvancedOptions, Color.FromArgb(20, 32, 47));
        }

        private static void TrySetButtonSurface(object button, Color background)
        {
            if (button == null) return;
            try
            {
                var prop = button.GetType().GetProperty("Image");
                if (prop != null && prop.CanWrite && prop.PropertyType.IsAssignableFrom(typeof(ACImage)))
                    prop.SetValue(button, new ACImage(background), null);
            }
            catch { /* Older VVS themes may not expose button images. */ }
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
            if (BrowseTemplate != null) BrowseTemplate.MouseEvent -= BrowseTemplate_MouseEvent;
            if (AdvancedOptions != null) AdvancedOptions.MouseEvent -= AdvancedOptions_MouseEvent;
            if (RefreshTemplate != null) RefreshTemplate.MouseEvent -= RefreshTemplate_MouseEvent;
            if (GenerateTemplate != null) GenerateTemplate.MouseEvent -= GenerateTemplate_MouseEvent;
            if (ApplyGeneratedTemplate != null) ApplyGeneratedTemplate.MouseEvent -= ApplyGeneratedTemplate_MouseEvent;
            if (view != null) view.Dispose();
            view = null;
            CurrentUiText = null;
            StatusText = StatusTextLine2 = StatusTextLine3 = null;
            ThemeChoice = null;
            ApplyTheme = ImportPack = RestoreVanilla = null;
            TemplatePngText = TemplateStatusIcon = null;
            TemplateStatusText = TemplateStatusDetail = null;
            BrandTitle = BrandSubtitle = TemplateLabel = null;
            BrowseTemplate = RefreshTemplate = GenerateTemplate = ApplyGeneratedTemplate = AdvancedOptions = null;
        }

        private void Core_RenderFrame(object sender, EventArgs e)
        {
            // A background compiler never touches VVS or native hooks. Consume its
            // completion exactly once here, on the normal Decal render thread.
            FinishTemplateBuildIfReady();
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
                if (advancedMode) BrowseMapping_Click();
                else BrowseTemplate_Click();
            }
        }

        private void AdvancedOptions_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType != ControlMouseEventArgs.MouseEventType.MouseHit) return;
            advancedMode = !advancedMode;
            RefreshTemplateStatus();
            SetStatus(advancedMode
                ? "Default mappings are included. Change only if needed."
                : BuildReadyStatus());
        }

        private void RefreshTemplate_MouseEvent(object sender, ControlMouseEventArgs e)
        {
            if (e.EventType == ControlMouseEventArgs.MouseEventType.MouseHit)
            {
                if (advancedMode) RestoreBundledMapping();
                else RefreshTemplateStatus();
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
                TemplateLabel.Text = advancedMode ? "MAPPING" : "THEME";
            TemplatePngText.Text = advancedMode
                ? (UsesBundledMapping() ? "Included mappings (default)"
                    : Shorten(System.IO.Path.GetFileName(selectedMappingJson), 22))
                : (String.IsNullOrWhiteSpace(selectedTemplatePng)
                    ? "Choose theme..."
                    : Shorten(System.IO.Path.GetFileName(selectedTemplatePng), 22));
            if (BrowseTemplate != null) BrowseTemplate.Text = advancedMode ? "Change Mapping" : "Browse";
            if (RefreshTemplate != null) RefreshTemplate.Text = advancedMode ? "Use Default" : "Refresh";
            if (AdvancedOptions != null) AdvancedOptions.Text = advancedMode ? "Done" : "Advanced";
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
