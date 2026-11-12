using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Security;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

namespace UsbInstaller
{
    class InstallException : Exception
    {
        public InstallException(string m) : base(m) { }
    }

    class Deployer
    {
        readonly InstallPlan plan;
        readonly AppConfig cfg;
        readonly Catalog cat;
        public Action<string, string, double> Report;

        static readonly Regex Pct = new Regex(@"(\d{1,3}(?:[.,]\d+)?)\s*%");
        string oldWindows;

        public Deployer(InstallPlan plan, AppConfig cfg, Catalog cat)
        {
            this.plan = plan;
            this.cfg = cfg;
            this.cat = cat;
        }

        bool Demo => Env.Demo;

        void R(string stage, string detail, double pct)
        {
            Report?.Invoke(stage, detail, pct);
        }

        void Simulate(string stage, string fmt, double from, double to, int ms)
        {
            int steps = Math.Max(1, ms / 60);
            for (int i = 0; i <= steps; i++)
            {
                double v = 100.0 * i / steps;
                R(stage, string.Format(fmt, v.ToString("0")), from + (to - from) * v / 100);
                Thread.Sleep(60);
            }
        }

        public void Run()
        {
            Log.W("=== Установка " + (Demo ? "(ДЕМО) " : "") + "===");
            Log.W("Образ: " + plan.ImageFile + " #" + plan.ImageIndex + " " + plan.ImageName + (plan.Lite ? " [Lite]" : ""));
            Log.W("Цель: " + plan.Target.Describe() + (Env.IsUefi ? " [UEFI]" : " [BIOS]"));
            Log.W("Программы: " + string.Join(", ", plan.Apps));
            Log.W("Настройки: " + string.Join(", ", plan.Tweaks));

            R("Подготовка диска", plan.Target.Describe(), 1);
            char w = 'W', s = 'S';
            if (plan.Target.KeepData)
            {

                w = char.ToUpperInvariant(plan.Target.Root[0]);
                R("Подготовка диска", "Переносим старую Windows в Windows.old", 1);
                oldWindows = Repair.MoveToWindowsOld(plan.Target.Root);
                s = Repair.FindBootPartition(new WinInstall { Root = plan.Target.Root });
            }
            else if (!Demo) DiskOps.PrepareForInstall(plan.Target, Env.IsUefi, out w, out s);
            else
            {

                try { DiskOps.PrepareForInstall(plan.Target, Env.IsUefi, out _, out _); }
                catch (Exception ex) { Log.W("Демо: разметка не прошла проверку: " + ex.Message); }
                Thread.Sleep(600);
            }
            string W = w + ":\\";
            if (!Demo && !Directory.Exists(W)) throw new InstallException("Раздел Windows (" + W + ") не появился после разметки.");

            Apply(W);
            RemoveApps(W);
            AddDrivers(W);

            R("Настройки Windows", "Применяем выбранные параметры", 88);
            OfflineTweaks(W);
            StartLayout(W);

            R("Программы " + cfg.BrandName, "Готовим установку программ после первого входа", 91);
            bool post = CopyPostSetup(W);

            R("Программы " + cfg.BrandName, "Записываем файл ответов", 95);
            WriteUnattend(W, post);

            R("Загрузчик", "Создаём загрузочную запись", 96);
            MakeBoot(W, s);

            if (!Demo)
                try
                {
                    string logDir = Path.Combine(W, @"ProgramData\" + cfg.BrandId + @"\Logs");
                    Directory.CreateDirectory(logDir);
                    File.Copy(Paths.LogFile, Path.Combine(logDir, "install.log"), true);
                }
                catch { }
            Log.W("=== Установка завершена ===");
            R("Готово", "Windows установлена", 100);
        }

        void Apply(string W)
        {
            const string stage = "Применение Windows";
            R(stage, "Распаковываем данные Windows: 0%", 3);
            if (Demo) { Simulate(stage, "Распаковываем данные Windows: {0}%", 3, 72, 9000); return; }
            if (plan.ImageFile == null) throw new InstallException("На флешке не найден sources\\install.wim или install.esd.");
            int code = Proc.Run("dism.exe", "/English /Apply-Image /ImageFile:\"" + plan.ImageFile + "\" /Index:" + plan.ImageIndex + " /ApplyDir:" + W,
                line =>
                {
                    var m = Pct.Match(line);
                    if (!m.Success) return;
                    double v = Fmt.ParseDouble(m.Groups[1].Value);
                    R(stage, "Распаковываем данные Windows: " + v.ToString("0") + "%", 3 + v * 0.69);
                });
            if (code != 0) throw new InstallException("DISM не смог применить образ (код " + code + "). Возможно, повреждён install.wim или мало места на диске.");
        }

        void RemoveApps(string W)
        {
            const string stage = "Оптимизация Windows";
            var names = new List<string>();
            if (plan.Lite || plan.Tweaks.Contains("removeConsumerApps")) names.AddRange(cfg.ConsumerApps);
            if (plan.Lite) names.AddRange(cfg.LiteApps);
            if (names.Count == 0) return;
            R(stage, "Ищем предустановленные приложения", 72);
            if (Demo) { Simulate(stage, "Удаляем лишние приложения: {0}%", 72, 82, 2500); return; }

            string o = Proc.Capture("dism.exe", "/English /Image:" + W + " /Get-ProvisionedAppxPackages", out _);
            var pkgs = Regex.Matches(o, @"PackageName\s*:\s*(\S+)").Cast<Match>().Select(m => m.Groups[1].Value).ToList();
            var targets = pkgs.Where(p => names.Any(n => p.StartsWith(n + "_", StringComparison.OrdinalIgnoreCase))).ToList();
            for (int i = 0; i < targets.Count; i++)
            {
                string display = targets[i].Split('_')[0];
                R(stage, "Удаляем " + display + " (" + (i + 1) + " из " + targets.Count + ")", 72 + 10.0 * i / targets.Count);
                Proc.Run("dism.exe", "/English /Image:" + W + " /Remove-ProvisionedAppxPackage /PackageName:" + targets[i]);
            }
        }

        void AddDrivers(string W)
        {
            string dir = Paths.DriversDir;
            if (dir == null || !Directory.Exists(dir)) return;
            bool any;
            try { any = Directory.EnumerateFiles(dir, "*.inf", SearchOption.AllDirectories).Any(); } catch { any = false; }
            if (!any) return;
            R("Драйверы", "Добавляем драйверы с флешки", 83);
            if (Demo) { Thread.Sleep(1200); return; }
            Proc.Run("dism.exe", "/English /Image:" + W + " /Add-Driver /Driver:\"" + dir + "\" /Recurse");
        }

        void Reg(string args)
        {
            if (Demo) { Log.W("reg " + args); return; }
            Proc.Run("reg.exe", args);
        }

        void Dword(string key, string name, int v) => Reg("add \"HKLM\\" + key + "\" /v \"" + name + "\" /t REG_DWORD /d " + v + " /f");
        void Sz(string key, string name, string v) => Reg("add \"HKLM\\" + key + "\" /v \"" + name + "\" /t REG_SZ /d \"" + v + "\" /f");

        void OfflineTweaks(string W)
        {
            var t = plan.Tweaks;
            bool lite = plan.Lite;
            const string SOFT = "OFF_SOFT", SYS = "OFF_SYS", DEF = "OFF_DEF";
            Reg("load HKLM\\" + SOFT + " \"" + W + "Windows\\System32\\config\\SOFTWARE\"");
            Reg("load HKLM\\" + SYS + " \"" + W + "Windows\\System32\\config\\SYSTEM\"");
            Reg("load HKLM\\" + DEF + " \"" + W + "Users\\Default\\NTUSER.DAT\"");
            try
            {
                string cdm = DEF + @"\Software\Microsoft\Windows\CurrentVersion\ContentDeliveryManager";
                string adv = DEF + @"\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced";

                if (t.Contains("localAccount")) Dword(SOFT + @"\Microsoft\Windows\CurrentVersion\OOBE", "BypassNRO", 1);

                if (t.Contains("uacNever"))
                {
                    Dword(SOFT + @"\Microsoft\Windows\CurrentVersion\Policies\System", "ConsentPromptBehaviorAdmin", 0);
                    Dword(SOFT + @"\Microsoft\Windows\CurrentVersion\Policies\System", "PromptOnSecureDesktop", 0);
                }

                if (t.Contains("removeOneDrive"))
                    Reg("delete \"HKLM\\" + DEF + "\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v OneDriveSetup /f");

                if (t.Contains("showExtensions")) Dword(adv, "HideFileExt", 0);

                if (t.Contains("noTelemetry") || lite)
                {
                    Dword(SOFT + @"\Policies\Microsoft\Windows\DataCollection", "AllowTelemetry", 0);
                    Dword(SOFT + @"\Policies\Microsoft\Windows\AdvertisingInfo", "DisabledByGroupPolicy", 1);
                    Dword(SYS + @"\ControlSet001\Services\DiagTrack", "Start", 4);
                    Dword(DEF + @"\Software\Microsoft\Windows\CurrentVersion\AdvertisingInfo", "Enabled", 0);
                }

                if (t.Contains("noWidgets") || lite)
                {
                    Dword(SOFT + @"\Policies\Microsoft\Dsh", "AllowNewsAndInterests", 0);
                    Dword(DEF + @"\Software\Policies\Microsoft\Windows\WindowsCopilot", "TurnOffWindowsCopilot", 1);
                    Dword(adv, "TaskbarDa", 0);
                    Dword(adv, "ShowCopilotButton", 0);
                }

                if (t.Contains("darkMode"))
                {
                    Dword(DEF + @"\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "AppsUseLightTheme", 0);
                    Dword(DEF + @"\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize", "SystemUsesLightTheme", 0);
                }

                if (t.Contains("removeConsumerApps") || lite)
                {
                    Dword(SOFT + @"\Policies\Microsoft\Windows\CloudContent", "DisableWindowsConsumerFeatures", 1);
                    foreach (var v in new[] { "SilentInstalledAppsEnabled", "PreInstalledAppsEnabled", "OemPreInstalledAppsEnabled", "SystemPaneSuggestionsEnabled",
                        "SubscribedContent-338388Enabled", "SubscribedContent-338389Enabled", "SubscribedContent-310093Enabled" })
                        Dword(cdm, v, 0);
                    Dword(adv, "Start_IrisRecommendations", 0);
                }

                if (lite)
                {
                    Dword(DEF + @"\Software\Microsoft\Windows\CurrentVersion\BackgroundAccessApplications", "GlobalUserDisabled", 1);
                    Dword(SOFT + @"\Policies\Microsoft\Edge", "StartupBoostEnabled", 0);
                    Dword(SOFT + @"\Policies\Microsoft\Edge", "BackgroundModeEnabled", 0);
                }

                if (StartPinsPolicy)
                {

                    string k = SOFT + @"\Microsoft\PolicyManager\current\device\Start";
                    Reg("add \"HKLM\\" + k + "\" /v ConfigureStartPins /t REG_SZ /d \"" + StartPinsJson.Replace("\"", "\\\"") + "\" /f");
                    Dword(k, "ConfigureStartPins_ProviderSet", 1);
                    Sz(k, "ConfigureStartPins_WinningProvider", "B5292708-1619-419B-9923-E5D9F3925E71");
                }

                if (t.Contains("pauseUpdates"))
                {
                    string k = SOFT + @"\Microsoft\WindowsUpdate\UX\Settings";
                    string now = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"), end = DateTime.UtcNow.AddDays(35).ToString("yyyy-MM-ddTHH:mm:ssZ");
                    Sz(k, "PauseUpdatesStartTime", now);
                    Sz(k, "PauseUpdatesExpiryTime", end);
                    Sz(k, "PauseFeatureUpdatesStartTime", now);
                    Sz(k, "PauseFeatureUpdatesEndTime", end);
                    Sz(k, "PauseQualityUpdatesStartTime", now);
                    Sz(k, "PauseQualityUpdatesEndTime", end);
                }
            }
            finally
            {
                foreach (var h in new[] { DEF, SYS, SOFT }) Reg("unload HKLM\\" + h);
            }
        }

        bool StartPinsPolicy => plan.Os == "Windows 11" && (plan.Lite || plan.Tweaks.Contains("removeConsumerApps"));

        const string StartPinsJson =
            "{\"pinnedList\":[" +
            "{\"desktopAppLink\":\"%ALLUSERSPROFILE%\\\\Microsoft\\\\Windows\\\\Start Menu\\\\Programs\\\\Microsoft Edge.lnk\"}," +
            "{\"desktopAppLink\":\"%APPDATA%\\\\Microsoft\\\\Windows\\\\Start Menu\\\\Programs\\\\File Explorer.lnk\"}," +
            "{\"packagedAppId\":\"windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel\"}," +
            "{\"packagedAppId\":\"Microsoft.WindowsStore_8wekyb3d8bbwe!App\"}," +
            "{\"packagedAppId\":\"Microsoft.WindowsNotepad_8wekyb3d8bbwe!App\"}," +
            "{\"packagedAppId\":\"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App\"}," +
            "{\"packagedAppId\":\"Microsoft.Windows.Photos_8wekyb3d8bbwe!App\"}," +
            "{\"packagedAppId\":\"Microsoft.Paint_8wekyb3d8bbwe!App\"}," +
            "{\"packagedAppId\":\"Microsoft.ScreenSketch_8wekyb3d8bbwe!App\"}," +
            "{\"packagedAppId\":\"Microsoft.WindowsTerminal_8wekyb3d8bbwe!App\"}]}";

        void StartLayout(string W)
        {
            if (!StartPinsPolicy) return;
            const string json = @"{
  ""pinnedList"": [
    { ""desktopAppLink"": ""%ALLUSERSPROFILE%\\Microsoft\\Windows\\Start Menu\\Programs\\Microsoft Edge.lnk"" },
    { ""desktopAppLink"": ""%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\File Explorer.lnk"" },
    { ""packagedAppId"": ""windows.immersivecontrolpanel_cw5n1h2txyewy!microsoft.windows.immersivecontrolpanel"" },
    { ""packagedAppId"": ""Microsoft.WindowsStore_8wekyb3d8bbwe!App"" },
    { ""packagedAppId"": ""Microsoft.WindowsNotepad_8wekyb3d8bbwe!App"" },
    { ""packagedAppId"": ""Microsoft.WindowsCalculator_8wekyb3d8bbwe!App"" },
    { ""packagedAppId"": ""Microsoft.Windows.Photos_8wekyb3d8bbwe!App"" },
    { ""packagedAppId"": ""Microsoft.Paint_8wekyb3d8bbwe!App"" },
    { ""packagedAppId"": ""Microsoft.ScreenSketch_8wekyb3d8bbwe!App"" },
    { ""packagedAppId"": ""Microsoft.WindowsTerminal_8wekyb3d8bbwe!App"" }
  ]
}";

            const string taskbar = @"<?xml version=""1.0"" encoding=""utf-8""?>
<LayoutModificationTemplate xmlns=""http://schemas.microsoft.com/Start/2014/LayoutModification""
    xmlns:defaultlayout=""http://schemas.microsoft.com/Start/2014/FullDefaultLayout""
    xmlns:start=""http://schemas.microsoft.com/Start/2014/StartLayout""
    xmlns:taskbar=""http://schemas.microsoft.com/Start/2014/TaskbarLayout"" Version=""1"">
  <CustomTaskbarLayoutCollection PinListPlacement=""Replace"">
    <defaultlayout:TaskbarLayout>
      <taskbar:TaskbarPinList>
        <taskbar:DesktopApp DesktopApplicationLinkPath=""%APPDATA%\Microsoft\Windows\Start Menu\Programs\File Explorer.lnk"" />
        <taskbar:DesktopApp DesktopApplicationLinkPath=""%ALLUSERSPROFILE%\Microsoft\Windows\Start Menu\Programs\Microsoft Edge.lnk"" />
        <taskbar:UWA AppUserModelID=""Microsoft.WindowsStore_8wekyb3d8bbwe!App"" />
      </taskbar:TaskbarPinList>
    </defaultlayout:TaskbarLayout>
  </CustomTaskbarLayoutCollection>
</LayoutModificationTemplate>";
            string dir = Path.Combine(W, @"Users\Default\AppData\Local\Microsoft\Windows\Shell");
            if (Demo) { Log.W("Раскладка «Пуска» и панели задач: " + dir); return; }
            try
            {
                Directory.CreateDirectory(dir);
                File.WriteAllText(Path.Combine(dir, "LayoutModification.json"), json, new UTF8Encoding(false));
                File.WriteAllText(Path.Combine(dir, "LayoutModification.xml"), taskbar, new UTF8Encoding(false));
            }
            catch (Exception ex) { Log.W("Раскладка «Пуска» не записана: " + ex.Message); }
        }

        bool CopyPostSetup(string W)
        {
            var apps = cat.All.Where(i => plan.Apps.Contains(i.Id) && i.Available).ToList();
            var postTweaks = plan.Tweaks.Where(Tweaks.IsPost).ToList();
            bool migrate = plan.Target.KeepData && plan.MigrateUsers.Count > 0;
            if (apps.Count == 0 && postTweaks.Count == 0 && !plan.Lite && !migrate && !StartPinsPolicy) return false;

            var sel = new Dictionary<string, object>
            {
                { "apps", apps.Select(a => a.Id).ToList() },
                { "tweaks", postTweaks },
                { "lite", plan.Lite },
                { "darkUi", Theme.Dark },
                { "edition", plan.ImageName },
                { "unlockStartPins", StartPinsPolicy },
            };
            if (migrate)
            {

                string rel = (oldWindows ?? Path.Combine(plan.Target.Root, "Windows.old")).Substring(2);
                sel["migrate"] = new Dictionary<string, object> { { "path", rel + @"\Users" }, { "users", plan.MigrateUsers } };
            }
            string dest = Path.Combine(W, @"ProgramData\" + cfg.BrandId);
            if (Demo) { Log.W("selection.json:\n" + Json.Write(sel)); Thread.Sleep(700); return true; }

            Directory.CreateDirectory(dest);
            File.Copy(Application.ExecutablePath, Path.Combine(dest, "PCInstaller.exe"), true);
            foreach (var f in new[] { "config.json", "catalog.json" })
            {
                string src = Path.Combine(Paths.DataDir, f);
                if (File.Exists(src)) File.Copy(src, Path.Combine(dest, f), true);
            }
            CopyDir(Paths.AssetsDir, Path.Combine(dest, "assets"));
            foreach (var a in apps.Where(a => a.FromUsb))
            {
                string src = Path.Combine(Paths.ProgramsDir ?? "", a.File);
                string dst = Path.Combine(dest, "Programs", a.File);
                R("Программы " + cfg.BrandName, "Копируем " + a.Name, 92);
                if (File.Exists(src)) { Directory.CreateDirectory(Path.GetDirectoryName(dst)); File.Copy(src, dst, true); }
                else if (Directory.Exists(src)) CopyDir(src, dst);
            }
            File.WriteAllText(Path.Combine(dest, "selection.json"), Json.Write(sel), new UTF8Encoding(false));
            return true;
        }

        static void CopyDir(string src, string dst)
        {
            if (!Directory.Exists(src)) return;
            Directory.CreateDirectory(dst);
            foreach (var f in Directory.GetFiles(src)) File.Copy(f, Path.Combine(dst, Path.GetFileName(f)), true);
            foreach (var d in Directory.GetDirectories(src)) CopyDir(d, Path.Combine(dst, Path.GetFileName(d)));
        }

        void WriteUnattend(string W, bool runPost)
        {
            const string comp = "processorArchitecture=\"amd64\" publicKeyToken=\"31bf3856ad364e35\" language=\"neutral\" versionScope=\"nonSxS\"";
            string user = SecurityElement.Escape(plan.UserName ?? "");
            bool local = user.Length > 0;
            var x = new StringBuilder();
            x.AppendLine("<?xml version=\"1.0\" encoding=\"utf-8\"?>");
            x.AppendLine("<unattend xmlns=\"urn:schemas-microsoft-com:unattend\" xmlns:wcm=\"http://schemas.microsoft.com/WMIConfig/2002/State\">");
            if (!string.IsNullOrEmpty(cfg.TimeZone))
            {
                x.AppendLine("  <settings pass=\"specialize\">");
                x.AppendLine("    <component name=\"Microsoft-Windows-Shell-Setup\" " + comp + ">");
                x.AppendLine("      <TimeZone>" + SecurityElement.Escape(cfg.TimeZone) + "</TimeZone>");
                x.AppendLine("    </component>");
                x.AppendLine("  </settings>");
            }
            x.AppendLine("  <settings pass=\"oobeSystem\">");
            if (!string.IsNullOrEmpty(cfg.Locale))
            {
                x.AppendLine("    <component name=\"Microsoft-Windows-International-Core\" " + comp + ">");
                if (!string.IsNullOrEmpty(cfg.InputLocale)) x.AppendLine("      <InputLocale>" + SecurityElement.Escape(cfg.InputLocale) + "</InputLocale>");
                x.AppendLine("      <SystemLocale>" + SecurityElement.Escape(cfg.Locale) + "</SystemLocale>");
                x.AppendLine("      <UserLocale>" + SecurityElement.Escape(cfg.Locale) + "</UserLocale>");
                x.AppendLine("    </component>");
            }
            x.AppendLine("    <component name=\"Microsoft-Windows-Shell-Setup\" " + comp + ">");
            x.AppendLine("      <OOBE>");
            x.AppendLine("        <HideEULAPage>true</HideEULAPage>");
            if (local)
            {
                x.AppendLine("        <HideOnlineAccountScreens>true</HideOnlineAccountScreens>");
                x.AppendLine("        <HideLocalAccountScreen>true</HideLocalAccountScreen>");
                x.AppendLine("        <ProtectYourPC>3</ProtectYourPC>");
            }
            x.AppendLine("      </OOBE>");
            if (local)
            {
                x.AppendLine("      <UserAccounts><LocalAccounts><LocalAccount wcm:action=\"add\">");
                x.AppendLine("        <Name>" + user + "</Name><DisplayName>" + user + "</DisplayName><Group>Administrators</Group>");
                x.AppendLine("        <Password><Value></Value><PlainText>true</PlainText></Password>");
                x.AppendLine("      </LocalAccount></LocalAccounts></UserAccounts>");
                x.AppendLine("      <AutoLogon><Username>" + user + "</Username><Enabled>true</Enabled><LogonCount>1</LogonCount>");
                x.AppendLine("        <Password><Value></Value><PlainText>true</PlainText></Password></AutoLogon>");
            }
            if (runPost)
            {
                string exe = "C:\\ProgramData\\" + cfg.BrandId + "\\PCInstaller.exe";
                x.AppendLine("      <FirstLogonCommands>");
                x.AppendLine("        <SynchronousCommand wcm:action=\"add\"><Order>1</Order>");
                x.AppendLine("          <CommandLine>cmd /c start \"\" \"" + exe + "\" --postsetup</CommandLine>");
                x.AppendLine("          <Description>" + SecurityElement.Escape(cfg.BrandName) + " setup</Description></SynchronousCommand>");
                x.AppendLine("      </FirstLogonCommands>");
            }
            x.AppendLine("    </component>");
            x.AppendLine("  </settings>");
            x.AppendLine("</unattend>");

            if (Demo) { Log.W("unattend.xml:\n" + x); return; }
            string dir = Path.Combine(W, @"Windows\Panther");
            Directory.CreateDirectory(dir);
            File.WriteAllText(Path.Combine(dir, "unattend.xml"), x.ToString(), new UTF8Encoding(false));
        }

        void MakeBoot(string W, char sys)
        {
            if (Demo) { Simulate("Загрузчик", "Создаём загрузочную запись", 96, 99, 900); return; }
            string fromImage = Path.Combine(W, @"Windows\System32\bcdboot.exe");
            string exe = File.Exists(fromImage) ? fromImage : "bcdboot.exe";
            int code = Proc.Run(exe, W + "Windows /s " + sys + ": /f " + (Env.IsUefi ? "UEFI" : "BIOS"));
            if (code != 0) throw new InstallException("Не удалось создать загрузчик (bcdboot, код " + code + ").");
        }
    }
}
