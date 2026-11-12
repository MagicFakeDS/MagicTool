using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Forms;

namespace UsbInstaller
{

    class RepairPage : WizardPage
    {
        List<WinInstall> found = new List<WinInstall>();
        int sel = -1;
        bool loading, busy;
        readonly List<ListRow> winRows = new List<ListRow>();
        readonly List<ListRow> actionRows = new List<ListRow>();
        readonly ThinProgress bar = new ThinProgress();
        string status = "", stage = "";
        Rectangle leftR, rightR;

        static readonly (string id, string title, string sub)[] Actions =
        {
            ("boot", "Починить загрузку", "Если Windows перестала запускаться: заново создаёт загрузчик"),
            ("health", "Восстановить системные файлы", "DISM и SFC по образу с флешки — 10–40 минут, данные не трогаются"),
            ("reinstall", "Переустановить с переносом данных", "Новая Windows, старая — в Windows.old, файлы пользователей переедут сами"),
            ("rescue", "Спасти файлы на другой диск", "Рабочий стол, Документы, Загрузки, Фото, Видео — на флешку или другой диск"),
            ("upgrade", "Ремонт с сохранением программ и паролей", "Запускается из работающей Windows: файл REPAIR-WINDOWS.cmd на флешке"),
        };

        public RepairPage(WizardForm w) : base(w)
        {
            Title = "Восстановление Windows";
            Subtitle = "Выберите установленную Windows слева и нужное действие справа.";
            AddNav("Готово");
            BtnNext.Visible = false;
            Controls.Add(bar);
            bar.Visible = false;
            for (int i = 0; i < Actions.Length; i++)
            {
                var a = Actions[i];
                var row = new ListRow { Title = a.title, Sub = a.sub };
                string id = a.id;
                row.Click += (s, e) => Run(id);
                actionRows.Add(row);
                Controls.Add(row);
            }
        }

        WinInstall Cur => sel >= 0 && sel < found.Count ? found[sel] : null;

        public override void OnEnter()
        {
            if (!loading && found.Count == 0) Reload();
            base.OnEnter();
        }

        protected override void OnBackClick() => W.ShowPage(0);

        void Reload()
        {
            loading = true;
            status = "Ищем установленную Windows…";
            new Thread(() =>
            {
                List<WinInstall> list;
                try { list = Repair.Find(); } catch (Exception ex) { Log.W("Поиск Windows: " + ex); list = new List<WinInstall>(); }
                BeginInvoke(new Action(() =>
                {
                    found = list;
                    loading = false;
                    sel = found.Count > 0 ? 0 : -1;
                    status = found.Count == 0 ? "Установленная Windows не найдена. Спасти файлы можно, если диск виден в списке накопителей." : "";
                    BuildRows();
                    DoLayout();
                }));
            }) { IsBackground = true }.Start();
        }

        protected override void LayoutBody(Rectangle r)
        {
            int leftW = (int)(r.Width * 0.42);
            leftR = new Rectangle(r.X, r.Y + 26, leftW, r.Height - 26 - 60);
            rightR = new Rectangle(r.X + leftW + 22, r.Y + 26, r.Width - leftW - 22, r.Height - 26 - 60);
            int rh = Math.Min(64, (rightR.Height - 8 * (Actions.Length - 1)) / Actions.Length);
            for (int i = 0; i < actionRows.Count; i++)
                actionRows[i].SetBounds(rightR.X, rightR.Y + i * (rh + 8), rightR.Width, rh);
            bar.SetBounds(0, r.Bottom - 18, Width, 8);
            BuildRows();
        }

        void BuildRows()
        {
            SuspendLayout();
            foreach (var c in winRows) { Controls.Remove(c); c.Dispose(); }
            winRows.Clear();
            for (int i = 0; i < found.Count && i < 6; i++)
            {
                var w = found[i];
                var row = new ListRow { Title = w.Title, Sub = w.Sub, Selected = i == sel, Bounds = new Rectangle(leftR.X, leftR.Y + i * 72, leftR.Width, 64) };
                int idx = i;
                row.Click += (s, e) => { if (busy) return; sel = idx; BuildRows(); };
                winRows.Add(row);
                Controls.Add(row);
            }
            foreach (var a in actionRows) a.Enabled = !busy && !loading;
            BtnBack.Enabled = !busy;
            ResumeLayout();
            Invalidate();
        }

        protected override void PaintBody(Graphics g)
        {
            var r = Body;
            Overline(g, "УСТАНОВЛЕННАЯ WINDOWS", leftR.X, r.Y, leftR.Width);
            Overline(g, "ЧТО СДЕЛАТЬ", rightR.X, r.Y, rightR.Width);
            if (found.Count == 0)
                Ui.Text(g, loading ? "Ищем…" : "Не найдена", Ui.F(10), new Rectangle(leftR.X, leftR.Y + 8, leftR.Width, 40), P.Muted, BackColor, TextFormatFlags.WordBreak);
            if (!string.IsNullOrEmpty(stage))
                Ui.GradText(g, stage, Ui.F(11, true), new RectangleF(0, r.Bottom - 52, Width, 24), P.Accent, P.Accent2);
            Ui.Text(g, status, Ui.F(9.5f), new Rectangle(0, r.Bottom - (string.IsNullOrEmpty(stage) ? 40 : 28), Width, 22), P.Muted, BackColor, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
        }

        void Report(string text, double pct)
        {
            BeginInvoke(new Action(() => { status = text; bar.Value = pct; Invalidate(); }));
        }

        void Background(string title, Func<string> work)
        {
            busy = true;
            stage = title;
            status = "";
            bar.Value = 0;
            bar.Visible = true;
            BuildRows();
            new Thread(() =>
            {
                string result = null, error = null;
                try { result = work(); }
                catch (Exception ex) { error = ex is InstallException || ex is DiskOpException ? ex.Message : "Непредвиденная ошибка: " + ex.Message; Log.W(title + ": " + ex); }
                BeginInvoke(new Action(() =>
                {
                    busy = false;
                    bar.Visible = false;
                    stage = "";
                    BuildRows();
                    if (error != null) { status = error; Dialog.Info(W, "Не получилось", error + "\n\nЖурнал: " + Paths.LogFile); }
                    else { status = result; Dialog.Info(W, title, result + (Env.Demo ? "\n\nДемо-режим: на дисках ничего не менялось, команды записаны в журнал." : "")); }
                    Invalidate();
                }));
            }) { IsBackground = true }.Start();
        }

        void Run(string id)
        {
            if (busy) return;
            var w = Cur;
            if (id == "upgrade")
            {
                Dialog.Info(W, "Ремонт с сохранением программ и паролей",
                    "Это «обновление поверх» от Microsoft: Windows переустанавливается, а программы, файлы, настройки, учётные записи и пароли остаются.\n\n" +
                    "Microsoft разрешает его только из работающей Windows, поэтому:\n" +
                    "1. Загрузите компьютер как обычно.\n2. Откройте флешку (раздел DATA).\n3. Запустите REPAIR-WINDOWS.cmd и подтвердите.\n\n" +
                    "Нужна та же версия или новее, тот же язык и редакция, что на флешке. Если Windows не загружается — используйте «Переустановить с переносом данных».");
                return;
            }
            if (w == null && id != "rescue") { Dialog.Info(W, "Windows не выбрана", "Слева нет найденной установки Windows."); return; }
            switch (id)
            {
                case "boot":
                    if (Dialog.Ask(W, "Починить загрузку?", "Для " + w.Title + " будет заново создан загрузчик. Файлы и программы не затрагиваются.", BtnKind.Primary, "Починить", "Отмена") == 0)
                        Background("Починка загрузки", () => { Repair.FixBoot(w, Report); return "Загрузчик для " + w.Title + " создан заново. Перезагрузите компьютер и извлеките флешку."; });
                    break;
                case "health":
                    if (Dialog.Ask(W, "Проверить системные файлы?", "DISM восстановит хранилище компонентов по образу с флешки, затем SFC проверит системные файлы " + w.Title + ".\n\nЭто 10–40 минут. Файлы и программы не затрагиваются.", BtnKind.Primary, "Начать", "Отмена") == 0)
                        Background("Восстановление системных файлов", () => { Repair.RestoreHealth(w, W.Images, Report); return "Проверка завершена. Подробный отчёт — в журнале " + Paths.LogFile + "."; });
                    break;
                case "rescue":
                    Rescue(w);
                    break;
                case "reinstall":
                    Reinstall(w);
                    break;
            }
        }

        void Rescue(WinInstall w)
        {
            if (w == null) { Dialog.Info(W, "Нечего спасать", "Не найдена Windows с папками пользователей."); return; }
            var drives = new List<DriveInfo>();
            foreach (var d in DriveInfo.GetDrives())
            {
                try
                {
                    if (!d.IsReady || d.DriveType == DriveType.CDRom || d.DriveType == DriveType.Ram) continue;
                    if (char.ToUpperInvariant(d.Name[0]) == w.Letter || char.ToUpperInvariant(d.Name[0]) == 'X') continue;
                    if (d.AvailableFreeSpace < 512L * 1024 * 1024) continue;
                    drives.Add(d);
                }
                catch { }
            }
            if (drives.Count == 0) { Dialog.Info(W, "Некуда копировать", "Подключите флешку или внешний диск с свободным местом и откройте раздел заново."); return; }
            var items = drives.Select(d => (d.Name.TrimEnd('\\') + "  " + SafeLabel(d), "свободно " + Fmt.Gb(d.AvailableFreeSpace) + " из " + Fmt.Gb(d.TotalSize))).ToList();
            int r = Dialog.Pick(W, "Куда скопировать файлы?", "Будут скопированы папки пользователей " + string.Join(", ", w.Users) + ": " + "Рабочий стол, Документы, Загрузки, Изображения, Видео, Музыка, Избранное, Сохранённые игры.", items, -1);
            if (r < 0) return;
            string dest = drives[r].RootDirectory.FullName;
            Background("Спасение файлов", () => "Файлы скопированы в " + Repair.RescueFiles(w, dest, Report));
        }

        static string SafeLabel(DriveInfo d)
        {
            try { return string.IsNullOrEmpty(d.VolumeLabel) ? "" : d.VolumeLabel; } catch { return ""; }
        }

        void Reinstall(WinInstall w)
        {
            const long need = 25L * 1024 * 1024 * 1024;
            string text = "На диск " + w.Letter + ": будет установлена новая Windows без форматирования:\n\n" +
                "• старая система переедет в " + w.Letter + ":\\Windows.old;\n" +
                "• после первого входа файлы пользователей (" + (w.Users.Count == 0 ? "нет" : string.Join(", ", w.Users)) + ") сами перенесутся в новый профиль;\n" +
                "• другие папки в корне диска (игры и т.п.) останутся на месте.\n\n" +
                "Программы, учётные записи и пароли так не сохранить — для этого нужен «Ремонт с сохранением программ» из работающей Windows.";
            if (w.FreeBytes < need) text += "\n\nВНИМАНИЕ: свободно только " + Fmt.Gb(w.FreeBytes) + ", для новой Windows нужно около 25 ГБ.";
            if (Dialog.Ask(W, "Переустановить с переносом данных?", text, BtnKind.Primary, "Дальше", "Отмена") != 0) return;
            W.Plan.Target = new DiskTarget { KeepData = true, Root = w.Root, DiskName = w.Title, DiskSize = w.TotalBytes };
            W.Plan.MigrateUsers = new List<string>(w.Users);
            if (W.OsList.Contains(w.Os)) W.SelectOs(w.Os);
            W.ShowPage(1);
        }
    }
}
