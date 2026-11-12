using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;

namespace UsbInstaller
{

    class WinInstall
    {
        public string Root;
        public int Build;
        public string EditionId = "";
        public List<string> Users = new List<string>();
        public long FreeBytes, TotalBytes;

        public char Letter => char.ToUpperInvariant(Root[0]);
        public string Os => Build >= 22000 ? "Windows 11" : "Windows 10";

        public string Edition
        {
            get
            {
                switch (EditionId)
                {
                    case "Professional": return "Pro";
                    case "Core": return "Домашняя";
                    case "CoreSingleLanguage": return "Домашняя для одного языка";
                    case "Education": return "для образования";
                    case "Enterprise": return "Корпоративная";
                    case "ProfessionalWorkstation": return "Pro для рабочих станций";
                    default: return EditionId;
                }
            }
        }

        public string Title => (Os + " " + Edition).Trim() + "  ·  диск " + Letter + ":";
        public string Sub => "Сборка " + (Build > 0 ? Build.ToString() : "?") + "  ·  пользователи: " + (Users.Count == 0 ? "нет" : string.Join(", ", Users)) + "  ·  свободно " + Fmt.Gb(FreeBytes);
    }

    static class Repair
    {
        static readonly string[] SkipUsers = { "Public", "Default", "Default User", "All Users", "defaultuser0", "WDAGUtilityAccount", "Общие" };

        public static readonly string[] UserFolders = { "Desktop", "Documents", "Downloads", "Pictures", "Videos", "Music", "Favorites", "Saved Games" };

        public static List<WinInstall> Find()
        {
            var list = new List<WinInstall>();
            string sysDrive = (Environment.GetEnvironmentVariable("SystemDrive") ?? "X:").TrimEnd('\\') + "\\";
            foreach (var d in DriveInfo.GetDrives())
            {
                try
                {
                    if (!d.IsReady || d.DriveType != DriveType.Fixed && d.DriveType != DriveType.Removable) continue;
                    string root = d.RootDirectory.FullName;

                    if (Env.IsWinPE && string.Equals(root, sysDrive, StringComparison.OrdinalIgnoreCase)) continue;

                    if (Paths.UsbRoot != null && string.Equals(Paths.UsbRoot.TrimEnd('\\') + "\\", root, StringComparison.OrdinalIgnoreCase)) continue;
                    string kernel = Path.Combine(root, @"Windows\System32\ntoskrnl.exe");
                    if (!File.Exists(kernel) || !File.Exists(Path.Combine(root, @"Windows\System32\config\SOFTWARE"))) continue;
                    var w = new WinInstall { Root = root, FreeBytes = d.AvailableFreeSpace, TotalBytes = d.TotalSize };
                    try { w.Build = FileVersionInfo.GetVersionInfo(kernel).FileBuildPart; } catch { }
                    w.EditionId = ReadEdition(root);
                    try
                    {
                        w.Users = Directory.GetDirectories(Path.Combine(root, "Users"))
                            .Select(Path.GetFileName)
                            .Where(n => !SkipUsers.Contains(n, StringComparer.OrdinalIgnoreCase) && !n.StartsWith("."))
                            .Where(n => Directory.Exists(Path.Combine(root, "Users", n, "Desktop")) || Directory.Exists(Path.Combine(root, "Users", n, "Documents")))
                            .ToList();
                    }
                    catch { }
                    list.Add(w);
                }
                catch (Exception ex) { Log.W("Поиск Windows на " + d.Name + ": " + ex.Message); }
            }
            return list;
        }

        static string ReadEdition(string root)
        {
            if (!Env.IsWinPE) return "";
            const string key = @"HKLM\PCI_REPAIR_SOFT";
            try
            {
                if (Proc.Run("reg.exe", "load " + key + " \"" + Path.Combine(root, @"Windows\System32\config\SOFTWARE") + "\"") != 0) return "";
                string o = Proc.Capture("reg.exe", "query \"" + key + "\\Microsoft\\Windows NT\\CurrentVersion\" /v EditionID", out _);
                var m = Regex.Match(o, @"EditionID\s+REG_SZ\s+(\S+)");
                return m.Success ? m.Groups[1].Value : "";
            }
            finally { Proc.Run("reg.exe", "unload " + key); }
        }

        static readonly Regex Pct = new Regex(@"(\d{1,3}(?:[.,]\d+)?)\s*%");

        public static void FixBoot(WinInstall w, Action<string, double> report)
        {
            report("Ищем загрузочный раздел", 10);
            char sys = FindBootPartition(w);
            report("Создаём загрузочную запись", 50);
            string bcdboot = Path.Combine(w.Root, @"Windows\System32\bcdboot.exe");
            if (!File.Exists(bcdboot)) bcdboot = "bcdboot.exe";
            string args = "\"" + Path.Combine(w.Root, "Windows") + "\" /s " + sys + ": /f " + (Env.IsUefi ? "UEFI" : "BIOS");
            if (Env.Demo) { Log.W("bcdboot " + args); Thread.Sleep(1500); }
            else if (Proc.Run(bcdboot, args) != 0) throw new InstallException("bcdboot не смог создать загрузчик. Подробности в журнале.");
            report("Загрузка восстановлена", 100);
        }

        public static char FindBootPartition(WinInstall w)
        {
            var disks = DiskManager.Load();
            var disk = disks.FirstOrDefault(d => d.Parts.Any(p => p.Letter == w.Letter));
            if (disk == null) throw new InstallException("Не удалось определить диск с Windows " + w.Letter + ":.");
            PartInfo boot = null;
            DiskInfo bootDisk = disk;
            if (Env.IsUefi)
            {
                boot = disk.Parts.FirstOrDefault(p => p.IsEsp);
                if (boot == null)
                    foreach (var d in disks.Where(d => !d.IsInstaller))
                        if ((boot = d.Parts.FirstOrDefault(p => p.IsEsp)) != null) { bootDisk = d; break; }
                if (boot == null) throw new InstallException("На дисках нет раздела EFI. Переустановите Windows на «весь диск».");
            }
            else
            {
                boot = disk.Parts.FirstOrDefault(p => p.IsActive) ?? disk.Parts.First(p => p.Letter == w.Letter);
            }
            if (boot.Letter != '\0') return boot.Letter;
            char letter = DiskOps.FreeLetter('S');
            DiskOps.RunDiskpart("select disk " + bootDisk.Number + "\r\nselect partition " + boot.Number + "\r\nassign letter=" + letter + "\r\n");
            return letter;
        }

        public static void RestoreHealth(WinInstall w, List<WimImage> images, Action<string, double> report)
        {
            var src = images.Where(i => i.Os == w.Os).OrderByDescending(i => i.EditionId == w.EditionId).ThenByDescending(i => i.EditionId == "Professional").FirstOrDefault();
            string scratch = Path.Combine(w.Root, @"Windows\Temp\PCInstallerScratch");
            string source = "";
            if (src != null)
                source = " /Source:" + (src.File.EndsWith(".esd", StringComparison.OrdinalIgnoreCase) ? "ESD:" : "WIM:") + "\"" + src.File + "\":" + src.Index + " /LimitAccess";
            else Log.W("На флешке нет образа " + w.Os + " — DISM возьмёт файлы только из хранилища компонентов.");

            report("Проверяем хранилище компонентов (DISM)", 2);
            if (Env.Demo) { for (int i = 0; i <= 100; i += 5) { report("DISM: восстановление компонентов " + i + "%", i * 0.6); Thread.Sleep(80); } }
            else
            {
                Directory.CreateDirectory(scratch);
                int code = Proc.Run("dism.exe", "/English /Image:" + w.Root + " /Cleanup-Image /RestoreHealth" + source + " /ScratchDir:\"" + scratch + "\"", line =>
                {
                    var m = Pct.Match(line);
                    if (m.Success) { double v = Fmt.ParseDouble(m.Groups[1].Value); report("DISM: восстановление компонентов " + v.ToString("0") + "%", v * 0.6); }
                });
                if (code != 0)
                    Log.W("DISM RestoreHealth завершился с кодом " + code + (src != null && src.Build < w.Build ? " (образ на флешке старее установленной Windows — часть файлов могла не найтись)" : ""));
            }

            report("Проверяем системные файлы (SFC)", 60);
            if (Env.Demo) { for (int i = 0; i <= 100; i += 5) { report("SFC: проверка " + i + "%", 60 + i * 0.4); Thread.Sleep(80); } }
            else
            {
                Proc.Run("sfc.exe", "/scannow /offbootdir=" + w.Root + " /offwindir=" + Path.Combine(w.Root, "Windows"), line =>
                {
                    var m = Pct.Match(line);
                    if (m.Success) { double v = Fmt.ParseDouble(m.Groups[1].Value); report("SFC: проверка " + v.ToString("0") + "%", 60 + v * 0.4); }
                }, System.Text.Encoding.Unicode);
            }
            report("Проверка завершена — подробности в журнале", 100);
        }

        public static string RescueFiles(WinInstall w, string destRoot, Action<string, double> report)
        {
            string dest = Path.Combine(destRoot, "Спасённые файлы " + DateTime.Now.ToString("yyyy-MM-dd HH-mm"));
            var jobs = new List<(string from, string to)>();
            foreach (var u in w.Users)
                foreach (var f in UserFolders)
                {
                    string from = Path.Combine(w.Root, "Users", u, f);
                    if (Directory.Exists(from)) jobs.Add((from, Path.Combine(dest, u, f)));
                }
            if (jobs.Count == 0) throw new InstallException("В профилях пользователей нет папок для копирования.");
            for (int i = 0; i < jobs.Count; i++)
            {
                var (from, to) = jobs[i];
                report("Копируем " + from.Substring(w.Root.Length), 100.0 * i / jobs.Count);
                if (Env.Demo) { Log.W("robocopy \"" + from + "\" \"" + to + "\""); Thread.Sleep(300); continue; }
                int code = Proc.Run("robocopy.exe", "\"" + from + "\" \"" + to + "\" /E /XJ /R:1 /W:1 /MT:8 /NP /NFL /NDL");
                if (code >= 8) Log.W("robocopy вернул " + code + " для " + from);
            }
            report("Готово: " + dest, 100);
            return dest;
        }

        public static string MoveToWindowsOld(string root)
        {
            string old = Path.Combine(root, "Windows.old");
            for (int n = 1; Directory.Exists(old); n++) old = Path.Combine(root, "Windows.old." + n);
            if (Env.Demo) { Log.W("Перенос старой системы в " + old); return old; }
            Directory.CreateDirectory(old);
            foreach (var name in new[] { "Windows", "Program Files", "Program Files (x86)", "ProgramData", "Users" })
            {
                string from = Path.Combine(root, name);
                if (!Directory.Exists(from)) continue;
                Log.W("Перемещаем " + from + " -> " + old);
                Directory.Move(from, Path.Combine(old, name));
            }
            return old;
        }
    }
}
