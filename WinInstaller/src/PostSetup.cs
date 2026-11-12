using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Net;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

namespace UsbInstaller
{
    enum StepStatus { Queued, Running, Done, Failed, Skipped }

    class PostStep
    {
        public string Name;
        public CatalogItem Item;
        public bool Migrate;
        public StepStatus Status;
        public string Error;
    }

    class StatusList : BaseControl
    {
        public List<PostStep> Steps = new List<PostStep>();
        public int Current = -1;
        Image logo;
        int scroll;
        float spin;
        const int RowH = 50;
        readonly System.Windows.Forms.Timer t = new System.Windows.Forms.Timer { Interval = 40 };

        public StatusList()
        {
            t.Tick += (s, e) => { spin = (spin + 12) % 360; if (Current >= 0) Invalidate(); };
            t.Start();
        }

        protected override void Dispose(bool disposing) { if (disposing) t.Dispose(); base.Dispose(disposing); }

        public void EnsureVisible(int i)
        {
            int top = i * RowH, bottom = top + RowH;
            if (top < scroll) scroll = top;
            else if (bottom > scroll + Height) scroll = bottom - Height;
            Invalidate();
        }

        protected override void OnMouseWheel(MouseEventArgs e)
        {
            int max = Math.Max(0, Steps.Count * RowH - Height);
            scroll = Math.Max(0, Math.Min(max, scroll - Math.Sign(e.Delta) * RowH));
            Invalidate();
            base.OnMouseWheel(e);
        }

        protected override void OnMouseEnter(EventArgs e) { Focus(); base.OnMouseEnter(e); }

        static Color ItemColor(CatalogItem it)
        {
            try { if (!string.IsNullOrEmpty(it.Color)) return ColorTranslator.FromHtml(it.Color); } catch { }
            return Theme.P.Accent;
        }

        static string Label(StepStatus s)
        {
            switch (s)
            {
                case StepStatus.Running: return "Устанавливается…";
                case StepStatus.Done: return "Установлено";
                case StepStatus.Failed: return "Ошибка";
                case StepStatus.Skipped: return "Пропущено";
                default: return "В очереди";
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            for (int i = 0; i < Steps.Count; i++)
            {
                var st = Steps[i];
                var r = new Rectangle(0, i * RowH - scroll, Width, RowH - 6);
                if (r.Bottom < 0 || r.Top > Height) continue;
                Color fill = Bg;
                if (st.Status == StepStatus.Running) { fill = p.AccentSoft; Ui.FillRoundGrad(g, r, 8, p.AccentSoft, p.AccentSoft2, 0f); }
                var ic = new Rectangle(12, r.Y + (r.Height - 22) / 2, 22, 22);
                switch (st.Status)
                {
                    case StepStatus.Done:
                        using (var b = Ui.Grad(ic, p.Accent, p.Accent2, 45f)) g.FillEllipse(b, ic);
                        Ui.Check(g, Rectangle.Inflate(ic, -1, -1), true);
                        break;
                    case StepStatus.Failed:
                        using (var b = new SolidBrush(p.Danger)) g.FillEllipse(b, ic);
                        using (var pen = new Pen(Color.White, 2f))
                        {
                            g.DrawLine(pen, ic.X + 7, ic.Y + 7, ic.Right - 7, ic.Bottom - 7);
                            g.DrawLine(pen, ic.Right - 7, ic.Y + 7, ic.X + 7, ic.Bottom - 7);
                        }
                        break;
                    case StepStatus.Running:
                        using (var pen = new Pen(Theme.Blend(p.Accent, fill, 0.25), 3f)) g.DrawEllipse(pen, Rectangle.Inflate(ic, -2, -2));
                        using (var pen = new Pen(p.Accent, 3f)) g.DrawArc(pen, Rectangle.Inflate(ic, -2, -2), spin, 100);
                        break;
                    case StepStatus.Skipped:
                        using (var pen = new Pen(p.Muted, 2f)) { g.DrawEllipse(pen, Rectangle.Inflate(ic, -1, -1)); g.DrawLine(pen, ic.X + 7, ic.Y + 11, ic.Right - 7, ic.Y + 11); }
                        break;
                    default:
                        using (var pen = new Pen(p.Border, 2f)) g.DrawEllipse(pen, Rectangle.Inflate(ic, -1, -1));
                        break;
                }
                var tile = new Rectangle(ic.Right + 10, r.Y + (r.Height - 30) / 2, 30, 30);
                if (st.Item != null) Icons.Tile(g, tile, Icons.Get(st.Item), st.Name.Substring(0, 1).ToUpperInvariant(), ItemColor(st.Item));
                else Icons.Tile(g, tile, logo ??= Ui.TryImage(Paths.AssetsDir, "logo.png"), "W", p.Accent2);
                int x = tile.Right + 10, w = Width - x - 10;
                Ui.Text(g, st.Name, Ui.F(9.5f, true), new Rectangle(x, r.Y + 6, w, 20), p.Text, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
                Ui.Text(g, Label(st.Status), Ui.F(8.5f), new Rectangle(x, r.Y + 24, w, 18), st.Status == StepStatus.Failed ? p.Danger : p.Muted, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
            }
        }
    }

    class PostSetupForm : Form
    {
        readonly AppConfig cfg;
        readonly Catalog cat;
        readonly List<PostStep> steps = new List<PostStep>();
        readonly List<string> tweaks;
        readonly bool lite;
        readonly bool unlockStartPins;

        void UnlockStartPins()
        {
            if (!unlockStartPins || demo) return;
            const string k = @"HKLM\SOFTWARE\Microsoft\PolicyManager\current\device\Start";
            foreach (var v in new[] { "ConfigureStartPins", "ConfigureStartPins_ProviderSet", "ConfigureStartPins_WinningProvider" })
                Proc.Run("reg.exe", "delete \"" + k + "\" /v " + v + " /f");
        }
        readonly string migratePath;
        readonly List<string> migrateUsers = new List<string>();
        readonly bool demo;

        readonly StatusList list = new StatusList();
        readonly ThinProgress overall = new ThinProgress(), itemBar = new ThinProgress();
        readonly FlatButton bLog = new FlatButton("Открыть журнал"), bSite = new FlatButton("Сайт программы"), bSkip = new FlatButton("Пропустить"),
            bExit = new FlatButton("Выход", BtnKind.Danger), bReboot = new FlatButton("Перезагрузить", BtnKind.Primary);
        readonly Image logo;

        int current = -1;
        double itemPct;
        string message = "", itemLog = "";
        bool finished, skipFlag, exitFlag;
        Process running;
        const int SideW = 320;

        public PostSetupForm(bool demo)
        {
            this.demo = demo;
            cfg = AppConfig.Load(Path.Combine(Paths.DataDir, "config.json"));
            cat = Catalog.Load(Path.Combine(Paths.DataDir, "catalog.json"), null);
            var sel = Json.ParseFile(Path.Combine(Paths.DataDir, "selection.json"));
            Theme.SetAccent2(cfg.Accent2);
            Theme.Init(cfg.Accent, sel?.Bool("darkUi", cfg.DarkByDefault) ?? cfg.DarkByDefault);
            tweaks = sel?.Strings("tweaks") ?? new List<string>();
            lite = sel?.Bool("lite") ?? false;
            unlockStartPins = sel?.Bool("unlockStartPins") ?? false;
            if (sel != null && sel.TryGetValue("migrate", out var mo) && mo is Dictionary<string, object> mig)
            {
                migratePath = mig.Str("path");
                migrateUsers = mig.Strings("users");
            }
            if (sel == null && demo)
            {
                tweaks = new List<string> { "noSleepAC", "showExtensions" };
                foreach (var it in cat.All.Where(i => i.Default)) steps.Add(new PostStep { Name = it.Name, Item = it });
            }
            if (!string.IsNullOrEmpty(migratePath) && migrateUsers.Count > 0) steps.Insert(0, new PostStep { Name = "Перенос файлов из старой Windows", Migrate = true });
            if (tweaks.Count > 0 || lite) steps.Insert(0, new PostStep { Name = "Настройки Windows" });
            if (sel != null)
                foreach (var id in sel.Strings("apps"))
                {
                    var it = cat.Find(id);
                    if (it != null) steps.Add(new PostStep { Name = it.Name, Item = it });
                }

            Text = cfg.BrandName + " — установка программ";
            Icon = Ui.AppIcon() ?? Icon;
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            ClientSize = new Size(1000, 600);
            BackColor = Theme.P.Card;
            Font = Ui.F(10);
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            logo = Ui.TryImage(Paths.AssetsDir, "logo.png");

            list.Steps = steps;
            Controls.AddRange(new Control[] { list, overall, itemBar, bLog, bSite, bSkip, bExit, bReboot });
            bReboot.Visible = false;
            bLog.Click += (s, e) => { string f = File.Exists(itemLog) ? itemLog : Paths.LogFile; Proc.Shell("notepad.exe", "\"" + f + "\""); };
            bSite.Click += (s, e) => { var it = Cur?.Item; if (!string.IsNullOrEmpty(it?.Site)) Proc.Shell(it.Site); };
            bSkip.Click += (s, e) => { if (finished) return; skipFlag = true; Proc.KillTree(running); };
            bExit.Click += (s, e) => Close();
            bReboot.Click += (s, e) => Proc.Shell("shutdown.exe", "/r /t 0");
            DoLayoutAll();
        }

        PostStep Cur => current >= 0 && current < steps.Count ? steps[current] : null;

        void DoLayoutAll()
        {
            list.SetBounds(20, 170, SideW - 40, ClientSize.Height - 190);
            int x = SideW + 40, w = ClientSize.Width - x - 40;
            overall.SetBounds(x + 20, 258, w - 40, 8);
            itemBar.SetBounds(x + 20, 318, w - 40, 6);
            int by = ClientSize.Height - 40 - 42;
            bLog.SetBounds(x, by, 150, 42);
            bSite.SetBounds(bLog.Right + 10, by, 150, 42);
            bExit.SetBounds(ClientSize.Width - 40 - 120, by, 120, 42);
            bSkip.SetBounds(bExit.Left - 10 - 140, by, 140, 42);
            bReboot.SetBounds(bExit.Left - 10 - 170, by, 170, 42);
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            Log.W("=== Мастер после установки" + (demo ? " (ДЕМО)" : "") + " ===");
            new Thread(Worker) { IsBackground = true }.Start();
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (!finished && e.CloseReason == CloseReason.UserClosing)
            {
                if (Dialog.Ask(this, "Прервать установку программ?", "Оставшиеся программы не будут установлены. Их можно поставить позже вручную.", BtnKind.DangerFill, "Прервать", "Продолжить установку") != 0)
                {
                    e.Cancel = true;
                    return;
                }
                exitFlag = true;
                skipFlag = true;
                Proc.KillTree(running);
            }
            base.OnFormClosing(e);
        }

        void UI(Action a)
        {
            if (IsDisposed) return;
            try { BeginInvoke(a); } catch { }
        }

        void SetMsg(string m) { message = m; UI(Invalidate); }

        void SetItem(double p) { itemPct = p; UI(() => { itemBar.Marquee = p < 0; if (p >= 0) itemBar.Value = p; UpdateOverall(); }); }

        void UpdateOverall()
        {
            int doneCount = steps.Count(s => s.Status != StepStatus.Queued && s.Status != StepStatus.Running);
            double frac = steps.Count == 0 ? 1 : (doneCount + (Cur?.Status == StepStatus.Running ? Math.Max(0, itemPct) / 100.0 : 0)) / steps.Count;
            overall.Value = frac * 100;
            Invalidate();
        }

        void Worker()
        {
            for (int i = 0; i < steps.Count && !exitFlag; i++)
            {
                var st = steps[i];
                current = i;
                skipFlag = false;
                itemPct = -1;
                message = "";
                st.Status = StepStatus.Running;
                itemLog = st.Item == null ? Paths.LogFile
                    : Path.Combine(Paths.ExeDir, @"Data\Temp", (st.Item.Type == "winget" ? "winget-" : "setup-") + Safe(st.Item.Id) + "-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".log");
                UI(() => { list.Current = i; list.EnsureVisible(i); bSite.Enabled = !string.IsNullOrEmpty(st.Item?.Site); SetItem(-1); });
                Log.W("--- " + st.Name);
                bool ok;
                try { ok = Execute(st); }
                catch (Exception ex) { ok = false; st.Error = ex.Message; Log.W("Ошибка: " + ex); }
                st.Status = skipFlag ? StepStatus.Skipped : ok ? StepStatus.Done : StepStatus.Failed;
                Log.W("    " + st.Status + (st.Error != null ? ": " + st.Error : ""));
                UI(() => { list.Invalidate(); UpdateOverall(); });
            }
            UnlockStartPins();
            current = -1;
            finished = true;
            UI(() =>
            {
                list.Current = -1;
                itemBar.Marquee = false;
                itemBar.Value = 100;
                overall.Value = 100;
                bSkip.Visible = false;
                bSite.Visible = false;
                bReboot.Visible = true;
                bExit.Text = "Закрыть";
                bExit.Kind = BtnKind.Secondary;
                Invalidate(true);
            });
        }

        static string Safe(string s) => Regex.Replace(s ?? "", @"[^\w\.\-]", "_");

        void ItemLog(string line)
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(itemLog));
                File.AppendAllText(itemLog, DateTime.Now.ToString("HH:mm:ss ") + line + Environment.NewLine, Encoding.UTF8);
            }
            catch { }
        }

        bool Execute(PostStep st)
        {
            if (demo)
            {
                for (int k = 0; k <= 40 && !skipFlag; k++) { SetItem(k * 2.5); Thread.Sleep(70); }
                return true;
            }
            if (st.Migrate) return MigrateFiles(st);
            if (st.Item == null) return ApplyTweaks();
            var it = st.Item;
            switch (it.Type)
            {
                case "winget": return Winget(st, it);
                case "url": return FromUrl(st, it);
                case "drivers": return Drivers(st, it);
                case "wudrivers": return WuDrivers(st);
                case "portable": return Portable(st, it);
                default: return Local(st, it);
            }
        }

        bool ApplyTweaks()
        {
            void Run(string f, string a) => Proc.Run(f, a, null, null, null, ItemLog);
            int n = 0, total = tweaks.Count + (lite ? 1 : 0);
            void Step(string m) { SetMsg(m); SetItem(total == 0 ? 100 : 100.0 * n++ / total); }
            bool explorer = false;
            foreach (var t in tweaks)
            {
                switch (t)
                {
                    case "highPerf":
                        Step("Схема электропитания «Высокая производительность»");
                        if (Proc.Run("powercfg.exe", "/setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c", null, null, null, ItemLog) != 0)
                        {
                            Run("powercfg.exe", "-duplicatescheme 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635d");
                            Run("powercfg.exe", "/setactive 8c5e7fda-e8bf-4a96-9a85-a6e23a8c635d");
                        }
                        break;
                    case "noSleepAC":
                        Step("Экран и сон от сети — никогда");
                        Run("powercfg.exe", "/change monitor-timeout-ac 0");
                        Run("powercfg.exe", "/change standby-timeout-ac 0");
                        Run("powercfg.exe", "/change hibernate-timeout-ac 0");
                        break;
                    case "removeOneDrive":
                        Step("Удаляем OneDrive");
                        Run("taskkill.exe", "/IM OneDrive.exe /F");
                        foreach (var setup in new[] { @"%SystemRoot%\System32\OneDriveSetup.exe", @"%SystemRoot%\SysWOW64\OneDriveSetup.exe", @"%LOCALAPPDATA%\Microsoft\OneDrive\OneDriveSetup.exe" })
                        {
                            string f = Environment.ExpandEnvironmentVariables(setup);
                            if (File.Exists(f)) { Run(f, "/uninstall"); break; }
                        }
                        Run("reg.exe", "delete \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\" /v OneDriveSetup /f");
                        break;
                    case "showExtensions":
                        Step("Показываем расширения файлов");
                        Run("reg.exe", "add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced\" /v HideFileExt /t REG_DWORD /d 0 /f");
                        explorer = true;
                        break;
                    case "classicMenu":
                        Step("Классическое контекстное меню");
                        Run("reg.exe", "add \"HKCU\\Software\\Classes\\CLSID\\{86ca1aa0-34aa-4e8b-a509-50c905bae2a2}\\InprocServer32\" /ve /f");
                        explorer = true;
                        break;
                    case "darkMode":
                        Step("Тёмная тема Windows");
                        Run("reg.exe", "add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize\" /v AppsUseLightTheme /t REG_DWORD /d 0 /f");
                        Run("reg.exe", "add \"HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize\" /v SystemUsesLightTheme /t REG_DWORD /d 0 /f");
                        explorer = true;
                        break;
                }
            }
            if (lite)
            {
                Step("Lite: отключаем гибернацию");
                Run("powercfg.exe", "-h off");
            }
            if (explorer)
            {
                Run("taskkill.exe", "/IM explorer.exe /F");
                Thread.Sleep(800);
                Proc.Shell(Environment.ExpandEnvironmentVariables(@"%SystemRoot%\explorer.exe"));
            }
            SetMsg("");
            return true;
        }

        static bool Online()
        {
            try
            {
                var rq = (HttpWebRequest)WebRequest.Create("http://www.msftconnecttest.com/connecttest.txt");
                rq.Timeout = 5000;
                using (var rs = (HttpWebResponse)rq.GetResponse()) return rs.StatusCode == HttpStatusCode.OK;
            }
            catch { return false; }
        }

        bool WaitOnline()
        {
            if (Online()) return true;
            SetMsg("Нет подключения к интернету. Подключите сеть (кабель или Wi-Fi) — установка продолжится автоматически. Или нажмите «Пропустить».");
            while (!skipFlag)
            {
                Thread.Sleep(4000);
                if (Online()) { SetMsg(""); return true; }
            }
            return false;
        }

        string wingetPath;

        bool EnsureWinget()
        {
            if (wingetPath != null && File.Exists(wingetPath)) return true;
            string cand = Environment.ExpandEnvironmentVariables(@"%LOCALAPPDATA%\Microsoft\WindowsApps\winget.exe");
            if (File.Exists(cand)) { wingetPath = cand; return true; }
            SetMsg("Ждём, пока Windows подготовит winget (Microsoft App Installer). Обычно это занимает 1–3 минуты после первого входа…");
            Proc.Run("powershell.exe", "-NoProfile -ExecutionPolicy Bypass -Command \"Add-AppxPackage -RegisterByFamilyName -MainPackage Microsoft.DesktopAppInstaller_8wekyb3d8bbwe\"", null, null, null, ItemLog);
            for (int i = 0; i < 12 && !skipFlag; i++)
            {
                if (File.Exists(cand)) { wingetPath = cand; SetMsg(""); return true; }
                Thread.Sleep(5000);
            }

            if (skipFlag || !WaitOnline()) return false;
            SetMsg("Устанавливаем winget (Microsoft App Installer) с серверов Microsoft…");
            string script = Path.Combine(Paths.ExeDir, @"Data\Temp\get-winget.ps1");
            Directory.CreateDirectory(Path.GetDirectoryName(script));
            File.WriteAllText(script, WingetBootstrap, new UTF8Encoding(true));
            Proc.Run("powershell.exe", "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"", ItemLog, Encoding.UTF8, p => running = p, ItemLog);
            running = null;
            for (int i = 0; i < 24 && !skipFlag; i++)
            {
                if (File.Exists(cand)) { wingetPath = cand; SetMsg(""); return true; }
                Thread.Sleep(5000);
            }
            return false;
        }

        const string WingetBootstrap = @"
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$dir = Join-Path $env:TEMP 'winget-bootstrap'
New-Item -ItemType Directory -Force $dir | Out-Null
$base = 'https://github.com/microsoft/winget-cli/releases/latest/download'
$bundle = Join-Path $dir 'AppInstaller.msixbundle'
$deps = Join-Path $dir 'deps.zip'
'download bundle'; Invoke-WebRequest ""$base/Microsoft.DesktopAppInstaller_8wekyb3d8bbwe.msixbundle"" -OutFile $bundle -UseBasicParsing
'download deps';   Invoke-WebRequest ""$base/DesktopAppInstaller_Dependencies.zip"" -OutFile $deps -UseBasicParsing
Expand-Archive $deps (Join-Path $dir 'deps') -Force
$appx = Get-ChildItem (Join-Path $dir 'deps') -Recurse -Filter *.appx | Where-Object { $_.FullName -match '\\x64\\' } | ForEach-Object FullName
'install'; Add-AppxPackage -Path $bundle -DependencyPath $appx -ForceApplicationShutdown
'done'
";

        static readonly Regex WgBytes = new Regex(@"([\d.,]+)\s*(KB|MB|GB)\s*/\s*([\d.,]+)\s*(KB|MB|GB)", RegexOptions.IgnoreCase);
        static readonly Regex WgPct = new Regex(@"(\d{1,3})\s*%");

        static double Bytes(string n, string u) => Fmt.ParseDouble(n) * (u.ToUpperInvariant() == "GB" ? 1024 : u.ToUpperInvariant() == "MB" ? 1 : 1 / 1024.0);

        bool Winget(PostStep st, CatalogItem it)
        {
            if (!WaitOnline()) return false;
            if (!EnsureWinget())
            {
                if (!string.IsNullOrEmpty(it.Url)) { ItemLog("winget недоступен — ставим по прямой ссылке " + it.Url); return FromUrl(st, it); }
                st.Error = "winget недоступен — установите «Установщик приложений» из Microsoft Store."; SetMsg(st.Error); return false;
            }
            string args = "install --id " + it.Winget + " -e --source winget --silent --accept-package-agreements --accept-source-agreements" + (string.IsNullOrEmpty(it.Args) ? "" : " " + it.Args);
            int code = Proc.Run(wingetPath, args, line =>
            {
                ItemLog(line);
                var m = WgBytes.Match(line);
                if (m.Success)
                {
                    double a = Bytes(m.Groups[1].Value, m.Groups[2].Value), b = Bytes(m.Groups[3].Value, m.Groups[4].Value);
                    if (b > 0) SetItem(Math.Min(95, a / b * 90));
                    return;
                }
                var p = WgPct.Match(line);
                if (p.Success) SetItem(Math.Min(95, Fmt.ParseDouble(p.Groups[1].Value) * 0.9));
                else if (line.IndexOf("install", StringComparison.OrdinalIgnoreCase) >= 0 || line.Contains("Установка")) SetItem(-1);
            }, Encoding.UTF8, p => running = p, ItemLog);
            running = null;

            if (code == 0 || code == unchecked((int)0x8A15002B) || code == unchecked((int)0x8A150061)) return true;

            if (!string.IsNullOrEmpty(it.Url) && !skipFlag)
            {
                ItemLog("winget вернул 0x" + code.ToString("X8") + " — ставим по прямой ссылке " + it.Url);
                SetMsg("winget не справился — ставим " + it.Name + " с официального сайта…");
                if (FromUrl(st, it)) { st.Error = null; return true; }
            }
            st.Error = "winget вернул код 0x" + code.ToString("X8") + (code == unchecked((int)0x8A150011) ? " (у производителя вышла новая версия, winget ещё не обновился — попробуйте позже)" : "") + ". Подробности в журнале.";
            SetMsg(st.Name + ": " + st.Error);
            return false;
        }

        bool FromUrl(PostStep st, CatalogItem it)
        {
            if (!WaitOnline()) return false;
            ServicePointManager.SecurityProtocol = (SecurityProtocolType)3072 | (SecurityProtocolType)12288;
            string name = Path.GetFileName(new Uri(it.Url).LocalPath);
            if (string.IsNullOrEmpty(Path.GetExtension(name))) name = Safe(it.Id) + ".exe";
            string file = Path.Combine(Paths.ExeDir, @"Data\Temp", name);
            Directory.CreateDirectory(Path.GetDirectoryName(file));
            SetMsg("Скачиваем " + it.Name + "…");
            ItemLog("GET " + it.Url);
            var rq = (HttpWebRequest)WebRequest.Create(it.Url);
            rq.UserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64)";
            using (var rs = rq.GetResponse())
            using (var src = rs.GetResponseStream())
            using (var dst = File.Create(file))
            {
                long total = rs.ContentLength, got = 0;
                var buf = new byte[81920];
                int n;
                while ((n = src.Read(buf, 0, buf.Length)) > 0)
                {
                    if (skipFlag) return false;
                    dst.Write(buf, 0, n);
                    got += n;
                    if (total > 0) SetItem(got * 60.0 / total);
                }
            }
            SetMsg("");
            SetItem(-1);
            return RunInstaller(st, file, it.Args);
        }

        bool Local(PostStep st, CatalogItem it)
        {
            string file = Path.Combine(Paths.ProgramsDir, it.File ?? "");
            if (!File.Exists(file)) { st.Error = "Файл не найден: " + file; SetMsg(st.Error); return false; }
            SetItem(-1);
            return RunInstaller(st, file, it.Args);
        }

        bool Portable(PostStep st, CatalogItem it)
        {
            string src = Path.Combine(Paths.ProgramsDir, it.File ?? "");
            if (!File.Exists(Path.Combine(src, it.Exe ?? ""))) { st.Error = "Файл не найден: " + Path.Combine(src, it.Exe ?? ""); SetMsg(st.Error); return false; }
            string dst = Environment.ExpandEnvironmentVariables(string.IsNullOrEmpty(it.Target) ? @"%SystemDrive%\" + Safe(it.Id) : it.Target);
            var files = Directory.GetFiles(src, "*", SearchOption.AllDirectories);
            SetMsg("Копируем " + it.Name + " в " + dst + "…");
            ItemLog("copy " + src + " -> " + dst);
            for (int i = 0; i < files.Length; i++)
            {
                if (skipFlag) return false;
                string to = Path.Combine(dst, files[i].Substring(src.Length).TrimStart('\\'));
                Directory.CreateDirectory(Path.GetDirectoryName(to));
                File.Copy(files[i], to, true);
                SetItem(90.0 * (i + 1) / files.Length);
            }
            string exe = Path.Combine(dst, it.Exe);
            string name = string.IsNullOrEmpty(it.Shortcut) ? it.Name : it.Shortcut;
            foreach (var dir in new[] { Environment.SpecialFolder.CommonDesktopDirectory, Environment.SpecialFolder.CommonPrograms })
            {
                try { MakeShortcut(Path.Combine(Environment.GetFolderPath(dir), string.Concat(name.Split(Path.GetInvalidFileNameChars())) + ".lnk"), exe, it.Description); }
                catch (Exception ex) { ItemLog("Ярлык не создан: " + ex.Message); }
            }
            SetMsg("");
            return true;
        }

        static void MakeShortcut(string lnk, string target, string description)
        {
            var t = Type.GetTypeFromProgID("WScript.Shell");
            object shell = Activator.CreateInstance(t);
            try
            {
                object sc = t.InvokeMember("CreateShortcut", System.Reflection.BindingFlags.InvokeMethod, null, shell, new object[] { lnk });
                var st = sc.GetType();
                void Set(string p, object v) => st.InvokeMember(p, System.Reflection.BindingFlags.SetProperty, null, sc, new[] { v });
                Set("TargetPath", target);
                Set("WorkingDirectory", Path.GetDirectoryName(target));
                Set("IconLocation", target + ",0");
                if (!string.IsNullOrEmpty(description)) Set("Description", description.Length > 250 ? description.Substring(0, 250) : description);
                st.InvokeMember("Save", System.Reflection.BindingFlags.InvokeMethod, null, sc, null);
                System.Runtime.InteropServices.Marshal.FinalReleaseComObject(sc);
            }
            finally { System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell); }
        }

        bool Drivers(PostStep st, CatalogItem it)
        {
            string dir = Path.Combine(Paths.ProgramsDir, it.File ?? "");
            if (!Directory.Exists(dir)) { st.Error = "Папка не найдена: " + dir; SetMsg(st.Error); return false; }
            SetItem(-1);
            int code = Proc.Run("pnputil.exe", "/add-driver \"" + Path.Combine(dir, "*.inf") + "\" /subdirs /install", ItemLog, null, p => running = p, ItemLog);
            running = null;
            return code == 0 || code == 3010 || code == 259;
        }

        const string WuScript = @"
$ErrorActionPreference = 'Stop'
$mu = '7971f918-a847-4430-9279-4a52d1efe18d'   # Microsoft Update — там лежат драйверы производителей
try { (New-Object -ComObject Microsoft.Update.ServiceManager).AddService2($mu, 7, '') | Out-Null } catch { }
$s = New-Object -ComObject Microsoft.Update.Session
$s.ClientApplicationID = 'PCInstaller'
$q = $s.CreateUpdateSearcher()
$q.ServiceID = $mu
$q.SearchScope = 1
$q.ServerSelection = 3
'SEARCH'
$list = @($q.Search(""IsInstalled=0 and Type='Driver' and IsHidden=0"").Updates)
'FOUND ' + $list.Count
$i = 0
foreach ($u in $list) {
    $i++
    'PROGRESS ' + $i + ' ' + $list.Count + ' ' + $u.Title
    try {
        try { if (-not $u.EulaAccepted) { $u.AcceptEula() } } catch { }
        $c = New-Object -ComObject Microsoft.Update.UpdateColl
        [void]$c.Add($u)
        $d = $s.CreateUpdateDownloader(); $d.Updates = $c; [void]$d.Download()
        $ins = $s.CreateUpdateInstaller(); $ins.Updates = $c
        'RESULT ' + $ins.Install().ResultCode + ' ' + $u.Title
    } catch { 'RESULT 4 ' + $u.Title + ' : ' + $_.Exception.Message }
}
";

        bool WuDrivers(PostStep st)
        {
            if (!WaitOnline()) return false;
            string script = Path.Combine(Paths.ExeDir, @"Data\Temp\wudrivers.ps1");
            Directory.CreateDirectory(Path.GetDirectoryName(script));
            File.WriteAllText(script, WuScript, new UTF8Encoding(true));
            int found = 0, ok = 0, bad = 0;
            int code = Proc.Run("powershell.exe", "-NoProfile -ExecutionPolicy Bypass -File \"" + script + "\"", line =>
            {
                ItemLog(line);
                var parts = line.Split(new[] { ' ' }, 4);
                switch (parts[0])
                {
                    case "SEARCH": SetMsg("Ищем драйверы для этого компьютера в Центре обновления Windows…"); SetItem(-1); break;
                    case "FOUND":
                        int.TryParse(parts.Length > 1 ? parts[1] : "0", out found);
                        SetMsg(found == 0 ? "Все драйверы уже на месте." : "Найдено драйверов: " + found);
                        break;
                    case "PROGRESS":
                        if (parts.Length >= 4 && int.TryParse(parts[1], out int i) && int.TryParse(parts[2], out int n) && n > 0)
                        { SetItem(100.0 * (i - 1) / n); SetMsg("Драйвер " + i + " из " + n + ": " + parts[3]); }
                        break;
                    case "RESULT":
                        if (parts.Length > 1 && (parts[1] == "2" || parts[1] == "3")) ok++; else bad++;
                        break;
                }
            }, Encoding.UTF8, p => running = p, ItemLog);
            running = null;
            SetMsg("");
            Log.W("Драйверы Windows Update: найдено " + found + ", установлено " + ok + ", ошибок " + bad + ", код " + code);
            if (code != 0 && found == 0) { st.Error = "Центр обновления недоступен (код " + code + "). Если включена пауза обновлений — драйверы поставятся после её окончания."; return false; }
            if (bad > 0) st.Error = "Не встали драйверы: " + bad + " из " + found + ". Подробности в журнале.";
            return bad == 0;
        }

        bool MigrateFiles(PostStep st)
        {
            string sysDrive = (Environment.GetEnvironmentVariable("SystemDrive") ?? "C:");
            string oldUsers = sysDrive + migratePath;
            string profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
            string docs = Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
            var jobs = new List<(string from, string to)>();
            for (int u = 0; u < migrateUsers.Count; u++)
            {
                string userDir = Path.Combine(oldUsers, migrateUsers[u]);
                foreach (var f in Repair.UserFolders.Concat(new[] { "OneDrive" }))
                {
                    string from = Path.Combine(userDir, f);
                    if (!Directory.Exists(from)) continue;
                    string to = u == 0
                        ? (f == "OneDrive" ? Path.Combine(profile, "Старый OneDrive") : Path.Combine(profile, f))
                        : Path.Combine(docs, "Файлы других пользователей", migrateUsers[u], f);
                    jobs.Add((from, to));
                }
            }
            if (jobs.Count == 0) { SetMsg("В старой Windows не нашлось файлов пользователей."); return true; }
            int bad = 0;
            for (int i = 0; i < jobs.Count && !skipFlag; i++)
            {
                var (from, to) = jobs[i];
                SetMsg("Переносим " + from.Substring(sysDrive.Length) + " → " + to);
                SetItem(100.0 * i / jobs.Count);
                if (demo) { Thread.Sleep(400); continue; }
                int code = Proc.Run("robocopy.exe", "\"" + from + "\" \"" + to + "\" /E /XJ /XC /XN /XO /R:1 /W:1 /MT:8 /NP /NFL /NDL", ItemLog, null, p => running = p, ItemLog);
                running = null;
                if (code >= 8) bad++;
            }
            SetMsg(bad == 0 ? "Файлы перенесены. Старая система осталась в " + sysDrive + Path.GetDirectoryName(migratePath) + " — её можно удалить через «Очистку диска»." : "");
            if (bad > 0) st.Error = "Часть папок не скопировалась (" + bad + "). Файлы остались в " + oldUsers + ".";
            return bad == 0;
        }

        bool RunInstaller(PostStep st, string file, string args)
        {
            string ext = Path.GetExtension(file).ToLowerInvariant();
            string exe = file, a = args ?? "";
            if (ext == ".msi") { exe = "msiexec.exe"; a = "/i \"" + file + "\" /qn /norestart " + a; }
            else if (ext == ".cmd" || ext == ".bat") { exe = "cmd.exe"; a = "/c \"\"" + file + "\" " + a + "\""; }
            else if (ext == ".ps1") { exe = "powershell.exe"; a = "-NoProfile -ExecutionPolicy Bypass -File \"" + file + "\" " + a; }
            SetMsg("Устанавливаем " + st.Name + "…");
            int code = Proc.Run(exe, a.Trim(), ItemLog, null, p => running = p, ItemLog);
            running = null;
            SetMsg("");
            if (code == 0 || code == 3010 || code == 1641) return true;
            st.Error = "Установщик вернул код " + code + ".";
            return false;
        }

        protected override void OnPaintBackground(PaintEventArgs e) { }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            g.Clear(p.Card);

            using (var b = new SolidBrush(p.Surface)) g.FillRectangle(b, 0, 0, SideW, ClientSize.Height);
            using (var pen = new Pen(p.Border)) g.DrawLine(pen, SideW, 0, SideW, ClientSize.Height);
            list.Fill = p.Surface;
            Ui.Logo(g, new Rectangle(20, 24, 34, 34), logo, cfg.BrandName);
            Ui.Text(g, "Настройка " + cfg.BrandName, Ui.F(12.5f, true), new Rectangle(64, 22, SideW - 80, 24), p.Text, p.Surface, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
            Ui.Text(g, "Драйверы, программы и компоненты", Ui.F(8.5f), new Rectangle(64, 45, SideW - 80, 18), p.Muted, p.Surface, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
            Ui.Text(g, "ПОСЛЕДОВАТЕЛЬНОСТЬ", Ui.F(8.5f, true), new Rectangle(20, 98, SideW - 40, 18), p.Muted, p.Surface, TextFormatFlags.SingleLine);
            Ui.Text(g, "Шагов: " + steps.Count + " — выполняются по очереди", Ui.F(9), new Rectangle(20, 120, SideW - 40, 36), p.Muted, p.Surface, TextFormatFlags.WordBreak);

            int x = SideW + 40, w = ClientSize.Width - x - 40;
            using (var b = Ui.Grad(new RectangleF(SideW, 0, ClientSize.Width - SideW, 4), p.Accent, p.Accent2, 0f)) g.FillRectangle(b, SideW + 1, 0, ClientSize.Width - SideW, 4);
            Ui.Text(g, "УСТАНОВКА ПРОГРАММ", Ui.F(8.5f, true), new Rectangle(ClientSize.Width - 300, 26, 260, 18), p.Muted, p.Card, TextFormatFlags.Right | TextFormatFlags.SingleLine);
            var cur = Cur;
            string over, title, sub;
            if (finished)
            {
                int ok = steps.Count(s => s.Status == StepStatus.Done), bad = steps.Count(s => s.Status == StepStatus.Failed), sk = steps.Count(s => s.Status == StepStatus.Skipped);
                over = "ГОТОВО";
                title = bad == 0 ? "Всё установлено" : "Установка завершена";
                sub = "Успешно: " + ok + (bad > 0 ? " · с ошибкой: " + bad : "") + (sk > 0 ? " · пропущено: " + sk : "") + ". Рекомендуем перезагрузить компьютер.";
            }
            else
            {
                over = cur == null ? "ПОДГОТОВКА" : "УСТАНАВЛИВАЕТСЯ";
                title = cur?.Name ?? "Подготовка…";
                sub = "Подробный журнал: " + itemLog;
            }
            Ui.Text(g, over, Ui.F(8.5f, true), new Rectangle(x, 60, w, 18), p.Accent, p.Card, TextFormatFlags.SingleLine);
            Ui.GradText(g, title, Ui.F(22, true), new RectangleF(x - 2, 82, w, 44), p.Text, Theme.Blend(p.Accent2, p.Text, 0.55));
            Ui.Text(g, sub, Ui.F(9), new Rectangle(x, 128, w, 40), p.Muted, p.Card, TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis);

            var box = new Rectangle(x, 196, w, 150);
            Ui.FillRound(g, box, 10, p.Surface);
            Ui.DrawRound(g, box, 10, p.Border);
            overall.Fill = p.Surface;
            itemBar.Fill = p.Surface;
            Ui.Text(g, "Общий прогресс", Ui.F(10, true), new Rectangle(x + 20, 216, w - 40, 22), p.Text, p.Surface, TextFormatFlags.SingleLine);
            Ui.Text(g, overall.Value.ToString("0") + "%", Ui.F(10, true), new Rectangle(x + 20, 216, w - 40, 22), p.Muted, p.Surface, TextFormatFlags.Right | TextFormatFlags.SingleLine);
            Ui.Text(g, finished ? "Все шаги выполнены" : "Текущий шаг", Ui.F(9), new Rectangle(x + 20, 290, w - 40, 20), p.Muted, p.Surface, TextFormatFlags.SingleLine);

            string note = message.Length > 0 ? message
                : finished ? (steps.Any(s => s.Status == StepStatus.Failed) ? "Программы с ошибкой можно установить позже вручную — причина записана в журнале." : "Можно закрыть это окно.")
                : "Если установка не завершится, здесь появится понятная причина.";
            bool warn = message.Length > 0 && (message.Contains("Нет подключения") || message.Contains("недоступен") || message.Contains("код"));
            Ui.Text(g, note, Ui.F(9.5f), new Rectangle(x, 366, w, 60), warn ? p.Danger : p.Muted, p.Card, TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis);
        }
    }
}
