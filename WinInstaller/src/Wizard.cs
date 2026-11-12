using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Linq;
using System.Threading;
using System.Windows.Forms;

namespace UsbInstaller
{
    class CardHost : Panel
    {
        public CardHost()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint, true);
        }
    }

    class WizardForm : Form
    {
        public readonly AppConfig Cfg;
        public readonly Catalog Cat;
        public List<WimImage> Images = new List<WimImage>();
        public readonly InstallPlan Plan = new InstallPlan();
        public bool Locked;

        readonly CardHost host = new CardHost();
        readonly List<WizardPage> pages = new List<WizardPage>();
        readonly Image bgLight, bgDark, logo;
        public int Current { get; private set; } = -1;
        public const int StepCount = 6;
        const int HeaderH = 78, Inset = 30;

        public WizardForm(AppConfig cfg, Catalog cat)
        {
            Cfg = cfg;
            Cat = cat;
            Text = cfg.BrandName + " — установка Windows";
            Icon = Ui.AppIcon() ?? Icon;
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
            KeyPreview = true;
            Font = Ui.F(10);
            if (Env.IsWinPE)
            {
                FormBorderStyle = FormBorderStyle.None;
                WindowState = FormWindowState.Maximized;
            }
            else
            {
                StartPosition = FormStartPosition.CenterScreen;
                ClientSize = new Size(1280, 800);
                MinimumSize = new Size(980, 700);
            }
            bgLight = Ui.TryImage(Paths.AssetsDir, "background.jpg", "background.png");
            bgDark = Ui.TryImage(Paths.AssetsDir, "background-dark.jpg", "background-dark.png") ?? bgLight;
            logo = Ui.TryImage(Paths.AssetsDir, "logo.png");

            foreach (var it in cat.All) if ((it.Default || it.DetectedDevice != null) && it.Available) Plan.Apps.Add(it.Id);
            foreach (var t in Tweaks.All) if (t.Default) Plan.Tweaks.Add(t.Id);
            LoadImages();

            Controls.Add(host);
            pages.Add(new WelcomePage(this));
            pages.Add(new VersionPage(this));
            pages.Add(new DiskPage(this));
            pages.Add(new AppsPage(this));
            pages.Add(new TweaksPage(this));
            pages.Add(new InstallPage(this));
            pages.Add(new RepairPage(this));
            ApplyTheme();
            ShowPage(0);
        }

        void LoadImages()
        {
            Images = Wim.GetAll(Paths.ImageFiles);
            if (Images.Count == 0 && Env.Demo)
                Images = new List<WimImage>
                {
                    new WimImage { Index = 1, Name = "Windows 11 Домашняя", Build = 26100 }, new WimImage { Index = 2, Name = "Windows 11 Pro", Build = 26100 },
                    new WimImage { Index = 1, Name = "Windows 10 Домашняя", Build = 19045 }, new WimImage { Index = 2, Name = "Windows 10 Pro", Build = 19045 },
                };
            SelectOs(Images.FirstOrDefault()?.Os);
        }

        public List<string> OsList => Images.Select(i => i.Os).Distinct().ToList();

        public void SelectOs(string os)
        {
            var list = Images.Where(i => i.Os == os).ToList();
            var def = list.FirstOrDefault(i => string.Equals(i.Name, Cfg.DefaultEdition, StringComparison.OrdinalIgnoreCase))
                   ?? list.FirstOrDefault(i => i.EditionId == "Professional")
                   ?? list.FirstOrDefault(i => i.Name != null && i.Name.EndsWith(" Pro", StringComparison.OrdinalIgnoreCase))
                   ?? list.FirstOrDefault();
            SelectImage(def);
        }

        public void SelectImage(WimImage img)
        {
            if (img == null) return;
            Plan.ImageIndex = img.Index;
            Plan.ImageName = img.Name;
            Plan.ImageFile = img.File;
            Plan.Os = img.Os;
        }

        public WizardPage Page(int i) => pages[i];

        public void ApplyTheme()
        {
            BackColor = Theme.P.Bg1;
            host.BackColor = Theme.P.Card;
            foreach (var p in pages) { p.BackColor = Theme.P.Card; p.ThemeChanged(); }
            Invalidate(true);
        }

        public void ShowPage(int i)
        {
            if (i < 0 || i >= pages.Count) return;
            host.SuspendLayout();
            if (Current >= 0) host.Controls.Remove(pages[Current]);
            Current = i;
            var p = pages[i];
            p.Dock = DockStyle.Fill;
            host.Controls.Add(p);
            host.ResumeLayout();
            LayoutCard();
            p.OnEnter();
            Invalidate();
        }

        bool KeepData => Plan.Target != null && Plan.Target.KeepData;

        public void Next() => ShowPage(Current == 1 && KeepData ? 3 : Current + 1);

        public void Back()
        {
            if (Current == 3 && KeepData) ShowPage(1);
            else if (Current == 1 && KeepData) { Plan.Target = null; Plan.MigrateUsers.Clear(); ShowPage(6); }
            else ShowPage(Current - 1);
        }

        Rectangle CardRect()
        {
            var c = ClientRectangle;
            if (Current == 0)
            {
                int w = Math.Min(520, c.Width - 80), h = Math.Min(470, c.Height - HeaderH - 40);
                int x = Math.Max(40, (int)(c.Width * 0.07));
                return new Rectangle(x, HeaderH + (c.Height - HeaderH - h) / 2, w, h);
            }
            if (Current == StepCount - 1)
            {
                int iw = Math.Min(940, c.Width - 64), ih = Math.Min(400, c.Height - HeaderH - 32);
                return new Rectangle((c.Width - iw) / 2, HeaderH + (c.Height - HeaderH - 16 - ih) / 2, iw, ih);
            }
            int cw = Math.Min(1180, c.Width - 64), ch = Math.Min(740, c.Height - HeaderH - 32);
            return new Rectangle((c.Width - cw) / 2, HeaderH + (c.Height - HeaderH - 16 - ch) / 2, cw, ch);
        }

        void LayoutCard()
        {
            if (Current < 0) return;
            var r = CardRect();
            host.Bounds = Rectangle.Inflate(r, -Inset, -Inset + 4);
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            LayoutCard();
            Invalidate();
        }

        protected override void OnPaintBackground(PaintEventArgs e) { }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            var c = ClientRectangle;
            if (c.Width < 10 || c.Height < 10) return;

            var bg = Theme.Dark ? bgDark : bgLight;
            if (bg != null)
            {
                float sc = Math.Max((float)c.Width / bg.Width, (float)c.Height / bg.Height);
                int w = (int)(bg.Width * sc), h = (int)(bg.Height * sc);
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.DrawImage(bg, (c.Width - w) / 2, (c.Height - h) / 2, w, h);
                using (var lg = new LinearGradientBrush(new Rectangle(0, 0, c.Width, HeaderH + 40), Color.FromArgb(Theme.Dark ? 210 : 190, p.Bg1), Color.FromArgb(0, p.Bg1), 90f))
                    g.FillRectangle(lg, 0, 0, c.Width, HeaderH + 40);
            }
            else PaintDecor(g, c, p);

            var lr = new Rectangle(34, 22, 36, 36);
            Ui.Logo(g, lr, logo, Cfg.BrandName);
            Ui.Text(g, Cfg.BrandName, Ui.F(15, true), new Rectangle(lr.Right + 12, 18, 400, 28), p.Text, TextFormatFlags.SingleLine);
            Ui.Text(g, Cfg.Tagline, Ui.F(7.5f, true), new Rectangle(lr.Right + 13, 45, 400, 16), p.Accent, TextFormatFlags.SingleLine);
            string step = Current == 6 ? "ВОССТАНОВЛЕНИЕ" : KeepData ? "ПЕРЕУСТАНОВКА С ПЕРЕНОСОМ ДАННЫХ  ·  ШАГ " + (Current + 1) + " ИЗ " + StepCount : "ШАГ " + (Current + 1) + " ИЗ " + StepCount;
            if (Env.Demo) step = "ДЕМО-РЕЖИМ   ·   " + step;
            Ui.Text(g, step, Ui.F(9, true), new Rectangle(c.Width - 434, 28, 400, 20), p.Muted, TextFormatFlags.Right | TextFormatFlags.SingleLine);

            var cr = CardRect();
            for (int i = 6; i >= 1; i--)
            {
                var sr = new Rectangle(cr.X - i, cr.Y - i + 6, cr.Width + i * 2, cr.Height + i * 2);
                Ui.FillRound(g, sr, 14 + i, Color.FromArgb(p.Shadow.A / 6, p.Shadow));
            }
            Ui.FillRound(g, cr, 14, p.Card);
            if (Theme.Dark) Ui.DrawRound(g, cr, 14, p.Border);
            var st = g.Save();
            using (var path = Ui.Round(cr, 14)) g.SetClip(path);
            using (var b = Ui.Grad(new RectangleF(cr.X, cr.Y, cr.Width, 5), p.Accent, p.Accent2, 0f)) g.FillRectangle(b, cr.X, cr.Y, cr.Width, 5);
            g.Restore(st);
        }

        void PaintDecor(Graphics g, Rectangle c, Palette p)
        {
            using (var lg = new LinearGradientBrush(c, p.Bg1, p.Bg2, 35f)) g.FillRectangle(lg, c);
            void Glow(Rectangle r, int alpha, Color col)
            {
                using (var path = new GraphicsPath())
                {
                    path.AddEllipse(r);
                    using (var pg = new PathGradientBrush(path) { CenterColor = Color.FromArgb(alpha, col), SurroundColors = new[] { Color.FromArgb(0, col) } })
                        g.FillEllipse(pg, r);
                }
            }
            Glow(new Rectangle((int)(c.Width * 0.50), (int)(-c.Height * 0.35), (int)(c.Width * 0.9), (int)(c.Height * 1.1)), Theme.Dark ? 80 : 60, p.Accent);
            Glow(new Rectangle((int)(c.Width * 0.62), (int)(c.Height * 0.35), (int)(c.Width * 0.7), (int)(c.Height * 0.9)), Theme.Dark ? 60 : 40, p.Accent2);
            Glow(new Rectangle((int)(-c.Width * 0.25), (int)(c.Height * 0.55), (int)(c.Width * 0.6), (int)(c.Height * 0.8)), Theme.Dark ? 45 : 30, p.Accent2);
            using (var pen = new Pen(Color.FromArgb(Theme.Dark ? 16 : 22, p.Accent), 1))
                for (int x = -c.Height; x < c.Width; x += 46)
                    g.DrawLine(pen, x, c.Height, x + c.Height, 0);

            if (Current == 0)
            {
                int h = (int)(c.Height * 0.62), w = (int)(h * 0.52);
                var r = new Rectangle((int)(c.Width * 0.66), (c.Height - h) / 2 + 30, w, h);
                Ui.FillRound(g, r, 12, Color.FromArgb(Theme.Dark ? 120 : 200, Theme.Dark ? Theme.Hex("#0E1319") : Theme.Hex("#F7F9FB")));
                Ui.DrawRound(g, r, 12, Color.FromArgb(120, p.Border), 2);
                var glass = Rectangle.Inflate(r, -18, -18);
                glass.Width = (int)(glass.Width * 0.72);
                Ui.FillRoundGrad(g, glass, 8, Color.FromArgb(Theme.Dark ? 80 : 45, p.Accent), Color.FromArgb(Theme.Dark ? 80 : 45, p.Accent2), 90f);
                for (int i = 0; i < 3; i++)
                {
                    var fan = new Rectangle(glass.X + glass.Width / 2 - glass.Width / 4, glass.Y + 20 + i * (glass.Height - 40) / 3, glass.Width / 2, glass.Width / 2);
                    using (var gb = Ui.Grad(fan, Color.FromArgb(Theme.Dark ? 200 : 160, p.Accent), Color.FromArgb(Theme.Dark ? 200 : 160, p.Accent2), 45f))
                    using (var pen = new Pen(gb, 3)) g.DrawEllipse(pen, fan);
                    using (var pen = new Pen(Color.FromArgb(Theme.Dark ? 90 : 70, p.Accent), 2)) g.DrawEllipse(pen, Rectangle.Inflate(fan, -fan.Width / 4, -fan.Width / 4));
                }
                using (var b = new SolidBrush(p.Accent)) g.FillEllipse(b, r.Right - 26, r.Y + 22, 9, 9);
            }
        }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.KeyCode == Keys.F10 && e.Shift) OpenCmd();
            base.OnKeyDown(e);
        }

        public void OpenCmd()
        {
            try { Process.Start("cmd.exe"); } catch { }
        }

        public void OpenLog()
        {
            try { Process.Start("notepad.exe", "\"" + Paths.LogFile + "\""); } catch { }
        }

        public void Reboot(bool shutdown = false)
        {
            if (Env.IsWinPE && !Env.Demo)
            {
                try { Process.Start(new ProcessStartInfo("wpeutil.exe", shutdown ? "shutdown" : "reboot") { CreateNoWindow = true, UseShellExecute = false }); } catch { }
                return;
            }
            Dialog.Info(this, "Демо-режим", shutdown ? "В настоящей установке здесь компьютер выключится." : "В настоящей установке здесь компьютер перезагрузится.");
        }

        public void AskExit()
        {
            if (!Env.IsWinPE)
            {
                if (Dialog.Ask(this, "Закрыть установщик?", "Установка ещё не начиналась, на дисках ничего не изменено.", BtnKind.Primary, "Закрыть", "Отмена") == 0) Close();
                return;
            }
            int r = Dialog.Ask(this, "Выход из установщика", "Установка ещё не начиналась, на дисках ничего не изменено. Что сделать?", BtnKind.Primary,
                "Перезагрузить", "Выключить", "Командная строка", "Отмена");
            if (r == 0) Reboot();
            else if (r == 1) Reboot(true);
            else if (r == 2) OpenCmd();
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (Locked && e.CloseReason == CloseReason.UserClosing) e.Cancel = true;
            base.OnFormClosing(e);
        }

        public void StartInstall()
        {
            Locked = true;
            ShowPage(5);
            ((InstallPage)pages[5]).Start();
        }
    }

    abstract class WizardPage : BaseControl
    {
        protected readonly WizardForm W;
        protected string Title, Subtitle;
        protected FlatButton BtnBack, BtnNext;
        protected const int TitleH = 92, FooterH = 44;

        protected WizardPage(WizardForm w)
        {
            W = w;
            BackColor = Theme.P.Card;
        }

        protected Palette P => Theme.P;

        protected void AddNav(string next = "Продолжить")
        {
            BtnBack = new FlatButton("Назад");
            BtnBack.Click += (s, e) => OnBackClick();
            BtnNext = new FlatButton(next, BtnKind.Primary);
            BtnNext.Click += (s, e) => OnNext();
            Controls.Add(BtnBack);
            Controls.Add(BtnNext);
        }

        protected virtual void OnNext() => W.Next();
        protected virtual void OnBackClick() => W.Back();
        public virtual void OnEnter() { DoLayout(); Invalidate(true); }
        public virtual void ThemeChanged() { Invalidate(true); }

        protected Rectangle Body => new Rectangle(0, TitleH, Width, Height - TitleH - FooterH - 20);

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            DoLayout();
        }

        protected void DoLayout()
        {
            if (Width < 50 || Height < 50) return;
            if (BtnNext != null)
            {
                int nw = Math.Max(160, BtnNext.PreferredWidth);
                BtnNext.SetBounds(Width - nw, Height - FooterH, nw, FooterH);
                BtnBack.SetBounds(BtnNext.Visible ? BtnNext.Left - 12 - 120 : Width - 120, Height - FooterH, 120, FooterH);
            }
            LayoutBody(Body);
            Invalidate();
        }

        protected virtual void LayoutBody(Rectangle r) { }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            if (Title != null)
            {
                Ui.Text(g, Title, Ui.F(22, true), new Rectangle(-2, -4, Width - 160, 48), P.Text, BackColor, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
                Ui.Text(g, Subtitle, Ui.F(10), new Rectangle(0, 46, Width - 170, 40), P.Muted, BackColor, TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis);
            }
            PaintBody(g);
        }

        protected virtual void PaintBody(Graphics g) { }

        protected void Overline(Graphics g, string s, int x, int y, int w, Color? c = null)
            => Ui.Text(g, s, Ui.F(8.5f, true), new Rectangle(x, y, w, 18), c ?? P.Muted, BackColor, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
    }

    class WelcomePage : WizardPage
    {
        readonly FlatButton start = new FlatButton("Начать установку", BtnKind.Primary);
        readonly FlatButton exit = new FlatButton("Выход");
        readonly FlatButton repair = new FlatButton("Восстановление");
        int infoY;

        public WelcomePage(WizardForm w) : base(w)
        {
            start.Click += (s, e) => { W.Plan.Target = null; W.Plan.MigrateUsers.Clear(); W.Next(); };
            exit.Click += (s, e) => W.AskExit();
            repair.Click += (s, e) => W.ShowPage(6);
            Controls.Add(start);
            Controls.Add(repair);
            Controls.Add(exit);
        }

        string OsTitle => W.OsList.Count > 1 ? "Windows 10 · 11" : W.OsList.FirstOrDefault() ?? "Windows 11";
        string Heading => OsTitle + "\n" + W.Cfg.BrandName;
        const string Desc = "Выберите версию Windows, драйверы и программы. Установка начнётся только после вашего подтверждения.";

        protected override void LayoutBody(Rectangle r)
        {
            int y = 30;
            y += Ui.TextHeight(Heading, Ui.F(28, true), Width) + 14;
            y += Ui.TextHeight(Desc, Ui.F(10.5f), Width) + 22;
            infoY = y;
            start.Width = Math.Max(180, start.PreferredWidth);
            start.SetBounds(0, Height - FooterH, start.Width, FooterH);
            repair.Width = Math.Max(150, repair.PreferredWidth);
            repair.SetBounds(start.Right + 12, Height - FooterH, repair.Width, FooterH);
            exit.SetBounds(repair.Right + 12, Height - FooterH, 100, FooterH);
        }

        protected override void PaintBody(Graphics g)
        {
            Ui.HQ(g);
            Overline(g, "СБОРКА  ·  ПРОВЕРКА  ·  УСТАНОВКА", 0, 0, Width, P.Accent);
            int y = 30;
            int h = Ui.TextHeight(Heading, Ui.F(28, true), Width);
            int h1 = Ui.TextHeight(OsTitle, Ui.F(28, true), Width);
            Ui.Text(g, OsTitle, Ui.F(28, true), new Rectangle(-2, y, Width, h1), P.Text, BackColor, TextFormatFlags.WordBreak);
            Ui.GradText(g, W.Cfg.BrandName, Ui.F(28, true), new RectangleF(-1, y + h1 + 2, Width, h - h1 + 10), P.Accent, P.Accent2);
            y += h + 14;
            h = Ui.TextHeight(Desc, Ui.F(10.5f), Width);
            Ui.Text(g, Desc, Ui.F(10.5f), new Rectangle(0, y, Width, h), P.Muted, BackColor, TextFormatFlags.WordBreak);

            var box = new Rectangle(0, infoY, Width, 84);
            bool bad = W.Images.Count == 0 || Paths.ImageFiles.Count == 0;
            Color fill = bad && !Env.Demo ? P.DangerSoft : P.Surface;
            Ui.FillRound(g, box, 8, fill);
            Ui.DrawRound(g, box, 8, bad && !Env.Demo ? P.Danger : P.Border);
            string l1, l2;
            if (bad && Env.Demo) { l1 = "Демо-режим"; l2 = "Диски не изменяются — можно спокойно смотреть интерфейс."; }
            else if (bad) { l1 = "Образ Windows не найден"; l2 = "Положите install.wim или install.esd в папку sources на флешке."; }
            else
            {
                l1 = "Исходный образ";
                l1 = "Образы на флешке";
                l2 = string.Join("  ·  ", W.OsList.Select(os => os + ": редакций " + W.Images.Count(i => i.Os == os))) + (Env.Demo ? "  ·  демо-режим" : "");
            }
            Ui.Text(g, l1, Ui.F(8.5f, true), new Rectangle(16, box.Y + 13, box.Width - 32, 18), bad && !Env.Demo ? P.Danger : P.Muted, fill, TextFormatFlags.SingleLine);
            Ui.Text(g, l2, Ui.F(10, true), new Rectangle(16, box.Y + 33, box.Width - 32, 42), P.Text, fill, TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis);
        }

        public override void OnEnter()
        {
            start.Enabled = Env.Demo || (Paths.ImageFiles.Count > 0 && W.Images.Count > 0);
            base.OnEnter();
        }
    }

    class VersionPage : WizardPage
    {
        readonly SelectCard std = new SelectCard
        {
            Overline = "STANDARD",
            Title = "Обычная",
        };
        readonly SelectCard lite = new SelectCard
        {
            Overline = "LITE",
            Title = "Lite",
            Description = "Для слабых ПК: меньше потребительских приложений и фоновых процессов. Store, безопасность, обновления и совместимость сохраняются.",
        };
        readonly FlatButton edition = new FlatButton("") { DropArrow = true };
        readonly FlatButton theme = new FlatButton("");
        readonly List<FlatButton> osButtons = new List<FlatButton>();
        int labelsY;

        public VersionPage(WizardForm w) : base(w)
        {
            Title = "Выберите версию Windows";
            Subtitle = "Обе версии создаются из одного проверенного исходного образа Microsoft.";
            std.Description = "Полный набор штатных компонентов Windows 11. Программы " + w.Cfg.BrandName + " устанавливаются только по выбранным галочкам.";
            AddNav();
            std.Click += (s, e) => { W.Plan.Lite = false; Sync(); };
            lite.Click += (s, e) => { W.Plan.Lite = true; Sync(); };
            edition.Click += (s, e) => PickEdition();
            theme.Click += (s, e) =>
            {
                Theme.Dark = !Theme.Dark;
                W.ApplyTheme();
                Sync();
            };
            Controls.AddRange(new Control[] { std, lite, edition, theme });
            foreach (var os in w.OsList)
            {
                var b = new FlatButton(os);
                b.Click += (s, e) => { W.SelectOs(os); Sync(); };
                osButtons.Add(b);
                Controls.Add(b);
            }
        }

        void Sync()
        {
            std.Description = "Полный набор штатных компонентов " + W.Plan.Os + ". Программы " + W.Cfg.BrandName + " устанавливаются только по выбранным галочкам.";
            foreach (var b in osButtons) b.Toggled = b.Text == W.Plan.Os;
            std.Selected = !W.Plan.Lite;
            lite.Selected = W.Plan.Lite;
            edition.Text = string.IsNullOrEmpty(W.Plan.ImageName) ? "Не выбрана" : W.Plan.ImageName;
            theme.Text = Theme.Dark ? "Тёмная тема" : "Светлая тема";
            BtnNext.Enabled = W.Images.Count > 0;
            DoLayout();
        }

        void PickEdition()
        {
            if (W.Images.Count == 0) return;
            var list = W.Images.Where(i => i.Os == W.Plan.Os).ToList();
            var items = list.Select(i => (i.Name, i.Os + (i.Build > 0 ? " · сборка " + i.Build : "") + " · индекс " + i.Index + (i.Size > 0 ? " · " + Fmt.Gb(i.Size) : ""))).ToList();
            int cur = list.FindIndex(i => i.Index == W.Plan.ImageIndex && i.File == W.Plan.ImageFile);
            int r = Dialog.Pick(W, "Редакция " + W.Plan.Os, "Выберите редакцию из образа. Ключ продукта не вводится — активация как обычно, после установки.", items, cur);
            if (r >= 0) { W.SelectImage(list[r]); Sync(); }
        }

        bool hwChecked;

        public override void OnEnter()
        {

            if (!hwChecked)
            {
                hwChecked = true;
                if (!Env.CpuSupportsWin11 && W.Plan.Os == "Windows 11" && W.OsList.Contains("Windows 10")) W.SelectOs("Windows 10");
                if (Env.RamGb > 0 && Env.RamGb < 4.5) W.Plan.Lite = true;
            }
            Sync();
            base.OnEnter();
        }

        string HardwareNote(out bool danger)
        {
            danger = false;
            if (W.Plan.Os == "Windows 11" && !Env.CpuSupportsWin11)
            {
                danger = true;
                return "Процессор этого ПК не поддерживает SSE4.2/POPCNT — Windows 11 на нём не загрузится. Выберите Windows 10.";
            }
            double ram = Env.RamGb;
            if (ram > 0 && ram < 4.5)
                return "Памяти " + ram.ToString("0.#") + " ГБ — для этого ПК рекомендуем версию Lite" + (W.Plan.Os == "Windows 11" && W.OsList.Contains("Windows 10") ? " или Windows 10." : ".");
            if (ram > 0) return "Этот ПК: память " + ram.ToString("0") + " ГБ, процессор подходит для " + (Env.CpuSupportsWin11 ? "Windows 10 и 11." : "Windows 10.");
            return null;
        }

        protected override void LayoutBody(Rectangle r)
        {
            int gap = 20, cw = (r.Width - gap) / 2;
            int ch = Math.Min(190, Math.Max(150, r.Height - 120));
            std.SetBounds(r.X, r.Y, cw, ch);
            lite.SetBounds(r.X + cw + gap, r.Y, r.Width - cw - gap, ch);
            labelsY = r.Y + ch + 26;
            int x = r.X;
            if (osButtons.Count > 1)
            {
                foreach (var b in osButtons)
                {
                    int bw = Math.Max(150, b.PreferredWidth + 10);
                    b.SetBounds(x, labelsY + 24, bw, 40);
                    x += bw + 8;
                }
                x += 24;
            }
            else foreach (var b in osButtons) b.Visible = false;
            edition.Width = Math.Max(240, edition.PreferredWidth + 40);
            edition.SetBounds(x, labelsY + 24, edition.Width, 40);
            theme.Width = Math.Max(170, theme.PreferredWidth);
            theme.SetBounds(edition.Right + 32, labelsY + 24, theme.Width, 40);
        }

        protected override void PaintBody(Graphics g)
        {
            if (osButtons.Count > 1) Overline(g, "СИСТЕМА", osButtons[0].Left, labelsY, 200);
            Overline(g, "РЕДАКЦИЯ", edition.Left, labelsY, 220);
            Overline(g, "ОФОРМЛЕНИЕ УСТАНОВЩИКА", theme.Left, labelsY, 260);
            string note = HardwareNote(out bool danger);
            if (note != null)
                Ui.Text(g, note, Ui.F(9.5f, danger), new Rectangle(0, labelsY + 84, Width, 40), danger ? P.Danger : P.Muted, BackColor, TextFormatFlags.WordBreak);
        }
    }

    class DiskPage : WizardPage
    {
        List<DiskInfo> disks = new List<DiskInfo>();
        bool loading, busy, loadedOnce;
        int diskSel = -1, rowSel = -1;
        bool whole;
        readonly List<object> rows = new List<object>();
        readonly List<ListRow> diskCtl = new List<ListRow>(), rowCtl = new List<ListRow>();
        readonly Pager diskPager = new Pager(), partPager = new Pager();
        readonly FlatButton bRefresh = new FlatButton("Обновить"), bCreate = new FlatButton("Создать"), bFormat = new FlatButton("Формат"),
            bDelete = new FlatButton("Удалить"), bWipe = new FlatButton("Стереть диск", BtnKind.Danger), bWhole = new FlatButton("Весь диск");
        Rectangle leftR, rightR;
        string status = "";
        bool statusDanger;
        const int DiskRowH = 60, PartRowH = 56, Gap = 8;

        public DiskPage(WizardForm w) : base(w)
        {
            Title = "Куда установить Windows?";
            Subtitle = "Слева — все физические накопители. Справа — разделы и незанятое пространство выбранного диска.";
            AddNav();
            Controls.AddRange(new Control[] { diskPager, partPager, bRefresh, bCreate, bFormat, bDelete, bWipe, bWhole });
            diskPager.PageChanged += (s, e) => BuildRows();
            partPager.PageChanged += (s, e) => BuildRows();
            bRefresh.Click += (s, e) => Reload();
            bWhole.Click += (s, e) => { whole = !whole; if (whole) rowSel = -1; BuildRows(); UpdateState(); };
            bCreate.Click += (s, e) => Op("create");
            bFormat.Click += (s, e) => Op("format");
            bDelete.Click += (s, e) => Op("delete");
            bWipe.Click += (s, e) => Op("wipe");
        }

        DiskInfo Disk => diskSel >= 0 && diskSel < disks.Count ? disks[diskSel] : null;
        object Row => rowSel >= 0 && rowSel < rows.Count ? rows[rowSel] : null;
        long MinBytes => (long)W.Cfg.MinDiskGB * 1073741824L;

        public override void OnEnter()
        {
            if (!loadedOnce) Reload();
            base.OnEnter();
            UpdateState();
        }

        public void LoadSync()
        {
            disks = DiskManager.Load();
            loadedOnce = true;
            AfterLoad(-1);
        }

        void Reload()
        {
            if (loading) return;
            loading = true;
            loadedOnce = true;
            int keep = Disk?.Number ?? -1;
            UpdateState();
            new Thread(() =>
            {
                List<DiskInfo> list;
                try { list = DiskManager.Load(); }
                catch (Exception ex) { Log.W("Список дисков: " + ex); list = new List<DiskInfo>(); }
                BeginInvoke(new Action(() => { disks = list; loading = false; AfterLoad(keep); }));
            }) { IsBackground = true }.Start();
        }

        void AfterLoad(int keepNumber)
        {
            diskSel = disks.FindIndex(d => d.Number == keepNumber);
            if (diskSel < 0) diskSel = disks.FindIndex(d => !d.IsInstaller);
            if (diskSel < 0 && disks.Count > 0) diskSel = 0;
            rowSel = -1;
            whole = Disk != null && !Disk.Detailed;
            BuildPartList();
            DoLayout();
            UpdateState();
        }

        void BuildPartList()
        {
            rows.Clear();
            var d = Disk;
            if (d == null || !d.Detailed) return;
            var all = new List<(long off, object o)>();
            foreach (var p in d.Parts) all.Add((p.Offset, p));
            foreach (var f in d.Free) all.Add((f.Offset, f));
            rows.AddRange(all.OrderBy(x => x.off).Select(x => x.o));
            partPager.Page = 0;
        }

        protected override void LayoutBody(Rectangle r)
        {
            int toolH = 40, statusH = 26;
            int leftW = (int)(r.Width * 0.36);
            int top = r.Y + 26;
            int h = r.Height - 26 - toolH - 12 - statusH;
            leftR = new Rectangle(r.X, top, leftW, h);
            rightR = new Rectangle(r.X + leftW + 22, top, r.Width - leftW - 22, h);
            diskPager.SetBounds(leftR.Right - 120, r.Y - 8, 120, 30);
            partPager.SetBounds(rightR.Right - 120, r.Y - 8, 120, 30);

            int ty = leftR.Bottom + 12;
            bRefresh.SetBounds(leftR.X, ty, 120, toolH);
            int x = rightR.Right;
            foreach (var b in new[] { bWhole, bWipe, bDelete, bFormat, bCreate })
            {
                int bw = Math.Max(b == bWhole ? 140 : 100, b.PreferredWidth);
                x -= bw;
                b.SetBounds(x, ty, bw, toolH);
                x -= 8;
            }
            BuildRows();
        }

        void BuildRows()
        {
            SuspendLayout();
            foreach (var c in diskCtl.Concat(rowCtl)) { Controls.Remove(c); c.Dispose(); }
            diskCtl.Clear();
            rowCtl.Clear();

            int per = Math.Max(1, (leftR.Height + Gap) / (DiskRowH + Gap));
            diskPager.Count = (disks.Count + per - 1) / per;
            diskPager.Visible = diskPager.Count > 1;
            for (int i = diskPager.Page * per, k = 0; i < disks.Count && k < per; i++, k++)
            {
                var d = disks[i];
                var row = new ListRow
                {
                    Title = d.Title,
                    Sub = string.Join(" · ", new[] { d.Bus, Fmt.Gb(d.Size), d.Style == "?" ? "" : d.Style }.Where(s => s.Length > 0)) + (d.IsInstaller ? " · " + d.ProtectReason : ""),
                    Unavailable = d.IsInstaller,
                    Selected = i == diskSel,
                    Bounds = new Rectangle(leftR.X, leftR.Y + k * (DiskRowH + Gap), leftR.Width, DiskRowH),
                };
                int idx = i;
                row.Click += (s, e) => { if (diskSel != idx) { diskSel = idx; rowSel = -1; whole = !disks[idx].Detailed; BuildPartList(); BuildRows(); UpdateState(); } };
                diskCtl.Add(row);
                Controls.Add(row);
            }

            per = Math.Max(1, (rightR.Height + Gap) / (PartRowH + Gap));
            partPager.Count = (rows.Count + per - 1) / per;
            partPager.Visible = partPager.Count > 1 && !whole;

            for (int i = partPager.Page * per, k = 0; !whole && i < rows.Count && k < per; i++, k++)
            {
                var row = new ListRow { Selected = i == rowSel && !whole, Bounds = new Rectangle(rightR.X, rightR.Y + k * (PartRowH + Gap), rightR.Width, PartRowH) };
                if (rows[i] is PartInfo p)
                {
                    string name = "Раздел " + p.Number + (p.Label.Length > 0 ? " · " + p.Label : "") + (p.Letter != '\0' ? " (" + p.Letter + ":)" : "");
                    row.Title = name;
                    var parts = new List<string> { p.Kind };
                    if (p.FileSystem.Length > 0) parts.Add(p.FileSystem);
                    if (p.FreeBytes >= 0 && !p.IsService) parts.Add("свободно " + Fmt.Gb(p.FreeBytes));
                    row.Sub = string.Join(" · ", parts);
                    row.RightText = Fmt.Gb(p.Size);
                    row.Unavailable = p.IsService;
                }
                else if (rows[i] is Extent f)
                {
                    row.Title = "Незанятое пространство";
                    row.Sub = "Можно установить сюда или создать раздел";
                    row.RightText = Fmt.Gb(f.Size);
                    row.RightSub = "свободно";
                    row.RightAccent = true;
                }
                int idx = i;
                row.Click += (s, e) => { rowSel = idx; whole = false; BuildRows(); UpdateState(); };
                rowCtl.Add(row);
                Controls.Add(row);
            }
            ResumeLayout();
            Invalidate();
        }

        string Check(out bool ok, out bool danger)
        {
            ok = false; danger = false;
            var d = Disk;
            if (loading) return "Ищем накопители…";
            if (busy) return "Выполняется операция с диском…";
            if (disks.Count == 0) return "Накопители не найдены. Если диск не виден — нужен драйвер контроллера (папка Drivers на флешке).";
            if (d == null) return "Выберите накопитель слева.";
            if (d.IsInstaller) return d.ProtectReason == "установочная флешка"
                ? "Это установочная флешка — на неё установить Windows нельзя."
                : "На этом диске лежит образ Windows — его нельзя стирать и ставить на него систему.";
            if (d.IsReadOnly) return "Диск доступен только для чтения.";
            if (whole)
            {
                if (d.Size < MinBytes) return "Диск слишком маленький: нужно минимум " + W.Cfg.MinDiskGB + " ГБ.";
                ok = true; danger = true;
                return "Windows будет установлена на весь " + d.Title + ". ВСЕ ДАННЫЕ НА ЭТОМ ДИСКЕ БУДУТ УДАЛЕНЫ.";
            }
            if (!d.Detailed) return "Для этого диска доступна только установка на весь диск.";
            var row = Row;
            bool uefi = Env.IsUefi;
            if (row == null)
                return rows.Count == 0 ? "На диске нет разделов. Нажмите «Весь диск»." : "Выберите раздел или незанятое пространство справа, либо нажмите «Весь диск».";
            if (uefi && d.Style == "MBR") return "Компьютер загружен в UEFI, а диск размечен в MBR. Нажмите «Весь диск», чтобы переразметить его в GPT.";
            if (!uefi && d.Style == "GPT") return "Компьютер загружен в режиме BIOS, а диск размечен в GPT. Нажмите «Весь диск».";
            if (row is PartInfo p)
            {
                if (p.IsService) return "Это служебный раздел (" + p.Kind + "). Выберите другой.";
                if (p.Size < MinBytes) return "Раздел слишком маленький: нужно минимум " + W.Cfg.MinDiskGB + " ГБ.";
                if (uefi && !disks.Any(x => !x.IsInstaller && x.Parts.Any(q => q.IsEsp)))
                    return "На дисках нет системного раздела EFI. Установите в незанятое пространство или на весь диск.";
                ok = true; danger = true;
                return "Раздел " + p.Number + " (" + Fmt.Gb(p.Size) + ") будет отформатирован — данные на нём удалятся. Остальные разделы не затрагиваются.";
            }
            var f = (Extent)row;
            long need = MinBytes + (uefi && !d.Parts.Any(q => q.IsEsp) ? 300L * 1048576 : 0);
            if (f.Size < need) return "Здесь слишком мало места: нужно минимум " + W.Cfg.MinDiskGB + " ГБ.";
            ok = true;
            return "Windows будет установлена в незанятое пространство (" + Fmt.Gb(f.Size) + "). Существующие разделы не затрагиваются.";
        }

        void UpdateState()
        {
            status = Check(out bool ok, out statusDanger);
            var d = Disk;
            bool usable = d != null && !d.IsInstaller && !loading && !busy;
            var row = Row;
            bWhole.Toggled = whole;
            bWhole.Enabled = usable;
            bWipe.Enabled = usable;
            bCreate.Enabled = usable && d.Detailed && !whole && row is Extent;
            bFormat.Enabled = usable && d.Detailed && !whole && row is PartInfo fp && !fp.IsMsr;
            bDelete.Enabled = usable && d.Detailed && !whole && row is PartInfo;
            bRefresh.Enabled = !loading && !busy;
            BtnNext.Enabled = ok && !busy && !loading;
            BtnBack.Enabled = !busy;
            DoLayout();
        }

        void Op(string kind)
        {
            var d = Disk;
            if (d == null) return;
            var row = Row;
            string q = null;
            Action act = null;
            switch (kind)
            {
                case "create" when row is Extent f:
                    act = () => DiskOps.CreatePartition(d, f);
                    break;
                case "format" when row is PartInfo p:
                    q = "Раздел " + p.Number + " (" + Fmt.Gb(p.Size) + ") на диске «" + d.Name + "» будет отформатирован. Все данные на нём будут удалены.";
                    act = () => DiskOps.FormatPartition(d.Number, p.Number);
                    break;
                case "delete" when row is PartInfo p:
                    q = "Раздел " + p.Number + " (" + Fmt.Gb(p.Size) + ") на диске «" + d.Name + "» будет удалён вместе со всеми данными." + (p.IsService ? "\n\nЭто служебный раздел — без него может не загрузиться другая установленная система." : "");
                    act = () => DiskOps.DeletePartition(d.Number, p.Number);
                    break;
                case "wipe":
                    q = "Диск «" + d.Name + "» (" + Fmt.Gb(d.Size) + ") будет полностью очищен: все разделы и данные будут удалены без возможности восстановления.";
                    act = () => DiskOps.WipeDisk(d.Number);
                    break;
            }
            if (act == null) return;
            if (q != null && Dialog.Ask(W, "Подтвердите действие", q, BtnKind.DangerFill, "Да, выполнить", "Отмена") != 0) return;
            busy = true;
            UpdateState();
            new Thread(() =>
            {
                string err = null;
                try { act(); } catch (Exception ex) { err = ex.Message; Log.W("Операция с диском: " + ex); }
                BeginInvoke(new Action(() =>
                {
                    busy = false;
                    if (err != null) Dialog.Info(W, "Не получилось", err);
                    else if (Env.Demo) Dialog.Info(W, "Демо-режим", "Операция не выполнялась: команды diskpart записаны в журнал " + Paths.LogFile);
                    Reload();
                }));
            }) { IsBackground = true }.Start();
        }

        protected override void OnNext()
        {
            Check(out bool ok, out _);
            if (!ok) return;
            var d = Disk;
            var t = new DiskTarget { Disk = d.Number, DiskName = d.Name, DiskSize = d.Size, DiskStyle = d.Style, WholeDisk = whole };
            if (!whole)
            {
                if (Row is PartInfo p) { t.Partition = p.Number; t.Size = p.Size; t.Offset = p.Offset; }
                else if (Row is Extent f) { t.FreeSpace = true; t.Offset = f.Offset; t.Size = f.Size; }
            }
            W.Plan.Target = t;
            W.Next();
        }

        protected override void PaintBody(Graphics g)
        {
            var r = Body;
            Overline(g, "НАКОПИТЕЛИ", r.X, r.Y, leftR.Width - 130);
            var d = Disk;
            Overline(g, d == null ? "РАЗДЕЛЫ" : (d.Title + " · " + Fmt.Gb(d.Size)).ToUpperInvariant(), rightR.X, r.Y, rightR.Width - 130);
            if (loading) Ui.Text(g, "Загрузка списка накопителей…", Ui.F(10), leftR, P.Muted, BackColor, TextFormatFlags.WordBreak);
            if (d != null && d.Detailed && rows.Count == 0 && !loading)
            {
                Ui.FillRound(g, new Rectangle(rightR.X, rightR.Y, rightR.Width, PartRowH), 8, P.Surface);
                Ui.Text(g, "Разделов нет — нажмите «Весь диск»", Ui.F(10), new Rectangle(rightR.X + 14, rightR.Y, rightR.Width - 28, PartRowH), P.Muted, P.Surface, TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
            }
            if (whole && d != null)
            {
                Ui.FillRound(g, new Rectangle(rightR.X, rightR.Y, rightR.Width, rightR.Height), 10, Theme.Blend(P.Danger, BackColor, 0.06));
                Ui.DrawRound(g, new Rectangle(rightR.X, rightR.Y, rightR.Width, rightR.Height), 10, Theme.Blend(P.Danger, BackColor, 0.5), 1.5f);
                var tr = new Rectangle(rightR.X + 28, rightR.Y + rightR.Height / 2 - 50, rightR.Width - 56, 100);
                Ui.Text(g, "Весь диск будет очищен", Ui.F(15, true), new Rectangle(tr.X, tr.Y, tr.Width, 32), P.Danger, TextFormatFlags.HorizontalCenter | TextFormatFlags.SingleLine);
                Ui.Text(g, (Env.IsUefi ? "Будут созданы разделы EFI, MSR и Windows на " : "Будут созданы разделы «Зарезервировано системой» и Windows на ") + Fmt.Gb(d.Size) + ".\nНажмите «Весь диск» ещё раз, чтобы выбрать раздел вручную.", Ui.F(10), new Rectangle(tr.X, tr.Y + 40, tr.Width, 60), P.Muted, TextFormatFlags.HorizontalCenter | TextFormatFlags.WordBreak);
            }
            Ui.Text(g, status, Ui.F(9.5f, statusDanger), new Rectangle(0, Height - FooterH - 32, Width, 22), statusDanger ? P.Danger : P.Muted, BackColor, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
        }
    }

    class AppsPage : WizardPage
    {
        readonly NavList nav = new NavList();
        readonly Pager pager = new Pager();
        readonly List<CheckCard> cards = new List<CheckCard>();
        readonly List<CatalogCategory> cats = new List<CatalogCategory>();
        readonly FlatButton devBtn = new FlatButton("Устройства ПК");
        Rectangle listR;
        const int CardH = 68, Gap = 8;

        public AppsPage(WizardForm w) : base(w)
        {
            Title = "Драйверы и программы";
            AddNav();

            var found = w.Cat.All.Where(i => i.DetectedDevice != null).ToList();
            if (found.Count > 0) cats.Add(new CatalogCategory { Name = "Найдено на этом ПК", Items = found });
            cats.AddRange(w.Cat.Categories);
            foreach (var c in cats) nav.Items.Add(c.Name);
            devBtn.Click += (s, e) =>
            {
                var usb = Devices.UsbSummary();
                Dialog.Info(W, "USB-устройства этого ПК", usb.Count == 0 ? "USB-устройства с VID/PID не найдены."
                    : string.Join("\n", usb.Take(14)) + "\n\nЧтобы программа выбиралась сама, добавьте ей в catalog.json правило \"detect\": [\"VID_xxxx&PID_xxxx\"] или [\"name:часть названия\"].");
            };
            Controls.Add(devBtn);
            nav.SelectedChanged += (s, e) => { pager.Page = 0; Build(); };
            pager.PageChanged += (s, e) => Build();
            Controls.Add(nav);
            Controls.Add(pager);
        }

        public void SelectCategory(int i) { if (i >= 0 && i < nav.Items.Count) nav.SelectedIndex = i; }

        void UpdateTexts()
        {
            nav.Counts = cats.Select(c => c.Items.Count(i => W.Plan.Apps.Contains(i.Id) && i.Available)).ToList();
            int n = W.Cat.All.Count(i => W.Plan.Apps.Contains(i.Id) && i.Available);
            Subtitle = "Выбрано: " + n + " · карточка выбирает пакет, кнопка справа показывает полное описание.";
            nav.Invalidate();
            Invalidate();
        }

        protected override void LayoutBody(Rectangle r)
        {
            int navW = 240;
            nav.SetBounds(r.X, r.Y, navW, r.Height);
            listR = new Rectangle(r.X + navW + 24, r.Y, r.Width - navW - 24, r.Height);
            pager.SetBounds(Width - 120, 44, 120, 32);
            devBtn.SetBounds(0, Height - FooterH, Math.Max(150, devBtn.PreferredWidth), FooterH);
            Build();
        }

        void Build()
        {
            SuspendLayout();
            foreach (var c in cards) { Controls.Remove(c); c.Dispose(); }
            cards.Clear();
            UpdateTexts();
            if (cats.Count == 0) { ResumeLayout(); return; }
            var cat = cats[Math.Min(nav.SelectedIndex, cats.Count - 1)];
            int per = Math.Max(1, (listR.Height + Gap) / (CardH + Gap));
            pager.Count = (cat.Items.Count + per - 1) / per;
            pager.Visible = pager.Count > 1;
            for (int i = pager.Page * per, k = 0; i < cat.Items.Count && k < per; i++, k++)
            {
                var it = cat.Items[i];
                var card = new CheckCard
                {
                    Title = it.Name,
                    Description = it.Description,
                    Note = it.DetectedDevice != null && !it.Available ? "Найдено: " + it.DetectedDevice + ", но установщика нет на флешке" : it.Missing,
                    Highlight = it.DetectedDevice != null ? "Найдено на этом ПК: " + it.DetectedDevice : null,
                    Unavailable = !it.Available,
                    ShowBadge = true,
                    ShowInfo = true,
                    Badge = it.Name.Length > 0 ? it.Name.Substring(0, 1).ToUpperInvariant() : "?",
                    BadgeColor = ParseColor(it.Color),
                    Icon = Icons.Get(it),
                    Data = it,
                    Bounds = new Rectangle(listR.X, listR.Y + k * (CardH + Gap), listR.Width, CardH),
                };
                card.Checked = W.Plan.Apps.Contains(it.Id) && it.Available;
                card.CheckedChanged += (s, e) =>
                {
                    if (card.Checked) W.Plan.Apps.Add(it.Id); else W.Plan.Apps.Remove(it.Id);
                    UpdateTexts();
                };
                card.InfoClicked += (s, e) =>
                    Dialog.Info(W, it.Name, it.Description + "\n\nИсточник: " + it.Source + (string.IsNullOrEmpty(it.Site) ? "" : "\nСайт: " + it.Site) + (it.Available ? "" : "\n\n" + it.Missing));
                cards.Add(card);
                Controls.Add(card);
            }
            ResumeLayout();
        }

        static Color ParseColor(string s)
        {
            try { if (!string.IsNullOrEmpty(s)) return ColorTranslator.FromHtml(s); } catch { }
            return Theme.P.Accent;
        }

        protected override void PaintBody(Graphics g)
        {
            string hint = "Программы ставятся после первого входа в Windows: через winget (нужен интернет) или с флешки.";
            Ui.Text(g, hint, Ui.F(9), new Rectangle(devBtn.Right + 16, Height - FooterH, Width - 320 - devBtn.Right - 16, FooterH), P.Muted, BackColor, TextFormatFlags.VerticalCenter | TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis);
        }
    }

    class TweaksPage : WizardPage
    {
        readonly List<CheckCard> cards = new List<CheckCard>();
        readonly Pager pager = new Pager();
        readonly TextField user = new TextField();
        Rectangle gridR;
        int userY;
        const int CardH = 66, Gap = 10;

        public TweaksPage(WizardForm w) : base(w)
        {
            Title = "Настройки Windows";
            Subtitle = "Рискованные изменения помечены знаком «!». Всё можно поменять позже в параметрах Windows.";
            AddNav("Установить");
            pager.PageChanged += (s, e) => Build();
            Controls.Add(pager);
            Controls.Add(user);
            user.Box.MaxLength = 20;
        }

        bool LocalOn => W.Plan.Tweaks.Contains("localAccount");

        protected override void LayoutBody(Rectangle r)
        {
            int userBlock = LocalOn ? 74 : 0;
            gridR = new Rectangle(r.X, r.Y, r.Width, r.Height - userBlock);
            userY = gridR.Bottom + 10;
            user.Visible = LocalOn;
            user.SetBounds(0, userY + 22, 300, 40);
            pager.SetBounds(Width - 120, 44, 120, 32);
            Build();
        }

        void Build()
        {
            SuspendLayout();
            foreach (var c in cards) { Controls.Remove(c); c.Dispose(); }
            cards.Clear();
            int rowsPer = Math.Max(1, (gridR.Height + Gap) / (CardH + Gap));
            int per = rowsPer * 2;
            var all = Tweaks.All;
            pager.Count = (all.Count + per - 1) / per;
            pager.Visible = pager.Count > 1;
            int cw = (gridR.Width - 16) / 2;
            for (int i = pager.Page * per, k = 0; i < all.Count && k < per; i++, k++)
            {
                var t = all[i];
                int col = k / rowsPer, row = k % rowsPer;
                var card = new CheckCard
                {
                    Title = t.Title,
                    Description = t.Desc,
                    Risky = t.Risky,
                    ShowInfo = true,
                    Bounds = new Rectangle(gridR.X + col * (cw + 16), gridR.Y + row * (CardH + Gap), cw, CardH),
                };
                card.Checked = W.Plan.Tweaks.Contains(t.Id);
                card.CheckedChanged += (s, e) =>
                {
                    if (card.Checked) W.Plan.Tweaks.Add(t.Id); else W.Plan.Tweaks.Remove(t.Id);
                    if (t.Id == "localAccount") BeginInvoke(new Action(DoLayout));
                };
                card.InfoClicked += (s, e) => Dialog.Info(W, t.Title, t.Desc + (t.Risky ? "\n\nВнимание: это изменение снижает безопасность или автономность." : ""));
                cards.Add(card);
                Controls.Add(card);
            }
            ResumeLayout();
        }

        protected override void PaintBody(Graphics g)
        {
            if (!LocalOn) return;
            Overline(g, "ИМЯ ПОЛЬЗОВАТЕЛЯ (НЕОБЯЗАТЕЛЬНО)", 0, userY, 400);
            Ui.Text(g, "Если указать — учётная запись создастся автоматически, без пароля, и Windows сразу войдёт в неё. Если оставить пустым — Windows спросит при первой настройке.",
                Ui.F(9), new Rectangle(user.Right + 18, userY + 18, Width - user.Right - 18, 48), P.Muted, BackColor, TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis);
        }

        protected override void OnNext()
        {
            string name = LocalOn ? user.Text.Trim() : "";
            if (name.Length > 0)
            {
                bool bad = !System.Text.RegularExpressions.Regex.IsMatch(name, @"^[A-Za-zА-Яа-яЁё0-9_.\- ]{1,20}$")
                    || new[] { "administrator", "администратор", "guest", "гость", "system", "user" }.Contains(name.ToLowerInvariant());
                if (bad)
                {
                    Dialog.Info(W, "Имя пользователя", "Используйте буквы, цифры, пробел, точку, дефис или подчёркивание (до 20 символов). Системные имена вроде Administrator нельзя.");
                    return;
                }
            }
            W.Plan.UserName = name;

            var p = W.Plan;
            int apps = W.Cat.All.Count(i => p.Apps.Contains(i.Id) && i.Available);
            string summary =
                "Версия: " + (p.Lite ? "Lite" : "Обычная") + " · " + p.ImageName + "\n" +
                "Куда: " + p.Target.Describe() + "\n" +
                "Программ: " + apps + " · Настроек: " + p.Tweaks.Count + (name.Length > 0 ? " · Пользователь: " + name : "") + "\n\n" +
                (p.Os == "Windows 11" && !Env.CpuSupportsWin11 ? "ВНИМАНИЕ: процессор этого ПК не поддерживает Windows 11 (нет SSE4.2/POPCNT) — после установки система не загрузится.\n\n" : "") +
                (Env.Demo ? "Демо-режим: диски не изменяются, установка будет показана как симуляция." : "После нажатия «Установить» отменить изменения на диске будет нельзя.");
            if (Dialog.Ask(W, "Начать установку?", summary, p.Target.WholeDisk || !p.Target.FreeSpace ? BtnKind.DangerFill : BtnKind.Primary, "Установить", "Отмена") == 0)
                W.StartInstall();
        }
    }

    class InstallPage : WizardPage
    {
        readonly ThinProgress bar = new ThinProgress();
        readonly FlatButton bReboot = new FlatButton("Перезагрузить", BtnKind.Primary), bLog = new FlatButton("Открыть журнал"), bCmd = new FlatButton("Командная строка");
        string stage = "Подготовка", detail = "";
        double pct;
        bool done, failed;
        int secs = 20;
        readonly System.Windows.Forms.Timer countdown = new System.Windows.Forms.Timer { Interval = 1000 };

        public InstallPage(WizardForm w) : base(w)
        {
            Title = "Устанавливаем " + w.Cfg.BrandName;
            Controls.AddRange(new Control[] { bar, bReboot, bLog, bCmd });
            bReboot.Visible = bLog.Visible = bCmd.Visible = false;
            bReboot.Click += (s, e) => { countdown.Stop(); W.Reboot(); };
            bLog.Click += (s, e) => { countdown.Stop(); W.OpenLog(); };
            bCmd.Click += (s, e) => W.OpenCmd();
            countdown.Tick += (s, e) =>
            {
                secs--;
                detail = "Компьютер перезагрузится через " + secs + " с. Флешку можно извлечь после перезагрузки.";
                Invalidate();
                if (secs <= 0) { countdown.Stop(); W.Reboot(); }
            };
        }

        public void Start()
        {
            var dep = new Deployer(W.Plan, W.Cfg, W.Cat);
            dep.Report = (st, de, p) => BeginInvoke(new Action(() => { stage = st; detail = de; pct = p; bar.Value = p; Invalidate(); }));
            new Thread(() =>
            {
                try
                {
                    dep.Run();
                    BeginInvoke(new Action(Finish));
                }
                catch (Exception ex)
                {
                    Log.W("ОШИБКА: " + ex);
                    BeginInvoke(new Action(() => Fail(ex is InstallException || ex is DiskOpException ? ex.Message : "Непредвиденная ошибка: " + ex.Message)));
                }
            }) { IsBackground = true }.Start();
        }

        void Finish()
        {
            done = true;
            Title = "Готово!";
            stage = "Windows установлена";
            detail = "Компьютер перезагрузится через " + secs + " с. Флешку можно извлечь после перезагрузки.";
            bar.Value = 100;
            bReboot.Visible = bLog.Visible = true;
            bReboot.Text = "Перезагрузить сейчас";
            W.Locked = false;
            countdown.Start();
            DoLayout();
        }

        void Fail(string msg)
        {
            failed = true;
            Title = "Установка прервана";
            stage = "Ошибка";
            detail = msg;
            bReboot.Visible = bLog.Visible = bCmd.Visible = true;
            W.Locked = false;
            DoLayout();
        }

        protected override void LayoutBody(Rectangle r)
        {
            bar.SetBounds(0, r.Y + 92, Width, 8);
            int x = Width;
            foreach (var b in new[] { bReboot, bLog, bCmd })
            {
                if (!b.Visible) continue;
                int bw = Math.Max(150, b.PreferredWidth);
                x -= bw;
                b.SetBounds(x, Height - FooterH, bw, FooterH);
                x -= 12;
            }
        }

        protected override void PaintBody(Graphics g)
        {
            var r = Body;
            Color sc = failed ? P.Danger : P.Accent;
            if (failed) Ui.Text(g, stage, Ui.F(16, true), new Rectangle(0, r.Y + 6, Width, 34), sc, BackColor, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
            else Ui.GradText(g, stage, Ui.F(16, true), new RectangleF(0, r.Y + 8, Width, 34), P.Accent, P.Accent2);
            Ui.Text(g, detail, Ui.F(10), new Rectangle(0, r.Y + 42, Width, failed ? 120 : 40), failed ? P.Text : P.Muted, BackColor, TextFormatFlags.WordBreak);
            if (!failed)
            {
                Ui.Text(g, pct.ToString("0") + "%", Ui.F(9.5f, true), new Rectangle(Width - 80, r.Y + 64, 80, 22), P.Muted, BackColor, TextFormatFlags.Right | TextFormatFlags.SingleLine);
                string foot = done ? "Журнал установки сохранён в C:\\ProgramData\\" + W.Cfg.BrandId + "\\Logs." : "Не выключайте компьютер. Все операции записываются в журнал установки.";
                Ui.Text(g, foot, Ui.F(9), new Rectangle(0, r.Y + 114, Width, 20), P.Muted, BackColor, TextFormatFlags.SingleLine);
            }
            bar.Visible = !failed;
        }
    }
}
