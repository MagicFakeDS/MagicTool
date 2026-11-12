using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Forms;

namespace UsbInstaller
{
    static class Program
    {

        [STAThread]
        static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            bool Has(string a) => args.Any(x => string.Equals(x, a, StringComparison.OrdinalIgnoreCase));
            string Arg(string a)
            {
                int i = Array.FindIndex(args, x => string.Equals(x, a, StringComparison.OrdinalIgnoreCase));
                return i >= 0 && i + 1 < args.Length ? args[i + 1] : null;
            }

            try
            {
                if (Has("--postsetup"))
                {
                    Env.Demo = Has("--demo");
                    Paths.InitPostSetup(Env.Demo);
                    Application.Run(new PostSetupForm(Env.Demo));
                    return 0;
                }

                Env.Demo = !Env.IsWinPE || Has("--demo");
                Paths.InitInstaller(Arg("--data"));
                Log.W("Запуск установщика. WinPE=" + Env.IsWinPE + " Demo=" + Env.Demo + " UEFI=" + Env.IsUefi + " Data=" + Paths.DataDir + " Images=" + string.Join("; ", Paths.ImageFiles));
                var cfg = AppConfig.Load(Path.Combine(Paths.DataDir, "config.json"));
                var cat = Catalog.Load(Path.Combine(Paths.DataDir, "catalog.json"), Paths.UsbRoot != null ? Paths.ProgramsDir : null);
                cat.DetectDevices();
                Theme.SetAccent2(cfg.Accent2);
                Theme.Init(cfg.Accent, cfg.DarkByDefault);

                string shot = Arg("--shot");
                if (shot != null) { Shots(shot, cfg, cat); return 0; }

                Application.Run(new WizardForm(cfg, cat));
                return 0;
            }
            catch (Exception ex)
            {
                Log.W("Фатальная ошибка: " + ex);
                MessageBox.Show(ex.ToString(), "Ошибка установщика", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }

        static void Shots(string dir, AppConfig cfg, Catalog cat)
        {
            Directory.CreateDirectory(dir);
            foreach (bool dark in new[] { false, true })
            {
                Theme.Init(cfg.Accent, dark);
                var f = new WizardForm(cfg, cat) { FormBorderStyle = FormBorderStyle.None, StartPosition = FormStartPosition.Manual, Location = new Point(-3000, -3000), ShowInTaskbar = false };
                f.ClientSize = new Size(1280, 800);
                f.Show();
                ((DiskPage)f.Page(2)).LoadSync();
                for (int i = 0; i <= WizardForm.StepCount; i++)
                {
                    f.ShowPage(i);
                    if (dark && f.Page(i) is AppsPage ap) ap.SelectCategory(cat.Categories.FindIndex(c => c.Items.Any(x => x.Id == "steam")));
                    Pump(300);
                    Save(f, Path.Combine(dir, (dark ? "dark" : "light") + "_" + (i + 1) + ".png"));
                }
                if (!dark)
                {

                    f.Plan.Target = new DiskTarget { Disk = 0, DiskName = "Demo SSD", DiskSize = 500L << 30, WholeDisk = true };
                    f.Plan.UserName = "Gamer";
                    f.Plan.Tweaks.Add("pauseUpdates");
                    f.StartInstall();
                    Pump(6000);
                    Save(f, Path.Combine(dir, "light_6_progress.png"));
                    for (int i = 0; i < 300 && f.Locked; i++) Pump(100);
                    Save(f, Path.Combine(dir, "light_6_done.png"));
                }
                f.Locked = false;
                f.Close();
            }
            Paths.InitPostSetup(true);
            Theme.Init(cfg.Accent, false);
            var ps = new PostSetupForm(true) { FormBorderStyle = FormBorderStyle.None, StartPosition = FormStartPosition.Manual, Location = new Point(-3000, -3000), ShowInTaskbar = false };
            ps.Show();
            Pump(2500);
            Save(ps, Path.Combine(dir, "postsetup.png"));
        }

        static void Pump(int ms)
        {
            var end = DateTime.Now.AddMilliseconds(ms);
            while (DateTime.Now < end) { Application.DoEvents(); Thread.Sleep(15); }
        }

        static void Save(Form f, string path)
        {
            using (var bmp = new Bitmap(f.Width, f.Height))
            {
                f.DrawToBitmap(bmp, new Rectangle(0, 0, f.Width, f.Height));
                bmp.Save(path, ImageFormat.Png);
            }
        }
    }
}
