using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using Microsoft.Win32;

namespace UsbInstaller
{

    static class Env
    {
        public static readonly bool IsWinPE = DetectPE();
        public static bool Demo;

        static bool DetectPE()
        {
            try
            {
                using (var k = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control\MiniNT"))
                    return k != null;
            }
            catch { return false; }
        }

        [DllImport("kernel32.dll")]
        static extern bool GetFirmwareType(out uint type);

        [DllImport("kernel32.dll")]
        static extern bool IsProcessorFeaturePresent(int feature);

        [StructLayout(LayoutKind.Sequential)]
        class MEMORYSTATUSEX { public uint dwLength = 64, dwMemoryLoad; public ulong ullTotalPhys, ullAvailPhys, ullTotalPageFile, ullAvailPageFile, ullTotalVirtual, ullAvailVirtual, ullAvailExtendedVirtual; }

        [DllImport("kernel32.dll")]
        static extern bool GlobalMemoryStatusEx([In, Out] MEMORYSTATUSEX m);

        public static bool CpuSupportsWin11
        {
            get { try { return IsProcessorFeaturePresent(38); } catch { return true; } }
        }

        public static double RamGb
        {
            get
            {
                try { var m = new MEMORYSTATUSEX(); return GlobalMemoryStatusEx(m) ? m.ullTotalPhys / 1073741824.0 : 0; }
                catch { return 0; }
            }
        }

        static bool? uefi;
        public static bool IsUefi
        {
            get
            {
                if (uefi.HasValue) return uefi.Value;
                try
                {
                    using (var k = Registry.LocalMachine.OpenSubKey(@"SYSTEM\CurrentControlSet\Control"))
                    {
                        var v = k?.GetValue("PEFirmwareType");
                        if (v is int i) { uefi = i == 2; return uefi.Value; }
                    }
                }
                catch { }
                try { if (GetFirmwareType(out uint t)) { uefi = t == 2; return uefi.Value; } } catch { }
                uefi = true;
                return true;
            }
        }
    }

    static class Paths
    {
        public static readonly string ExeDir = AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');

        public static string DataDir;

        public static string UsbRoot;

        public static readonly List<string> ImageFiles = new List<string>();
        public static string ImageFile => ImageFiles.FirstOrDefault();
        public static string ProgramsDir;
        public static string LogFile;

        public static string AssetsDir => Path.Combine(DataDir, "assets");
        public static string DriversDir => UsbRoot == null ? null : Path.Combine(UsbRoot, "Drivers");

        public static void InitInstaller(string dataArg)
        {
            string root = null;
            if (!string.IsNullOrEmpty(dataArg) && Directory.Exists(dataArg)) root = Path.GetFullPath(dataArg);

            if (root == null)
            {

                string fallback = null;
                foreach (var d in DriveInfo.GetDrives())
                {
                    try
                    {
                        if (!d.IsReady) continue;
                        string r = d.RootDirectory.FullName;
                        if (!File.Exists(Path.Combine(r, @"installer\catalog.json"))) continue;
                        if (FindImage(r) != null) { root = r; break; }
                        if (fallback == null) fallback = r;
                    }
                    catch { }
                }
                if (root == null) root = fallback;
            }

            if (root == null)
            {

                var dir = new DirectoryInfo(ExeDir);
                for (int i = 0; i < 6 && dir != null; i++, dir = dir.Parent)
                {
                    string cand = Path.Combine(dir.FullName, "usb");
                    if (File.Exists(Path.Combine(cand, @"installer\catalog.json"))) { root = cand; break; }
                }
            }

            if (root != null)
            {
                UsbRoot = root;
                DataDir = Path.Combine(root, "installer");
                ProgramsDir = Path.Combine(root, "Programs");
                ImageFiles.AddRange(FindImages(root));
            }
            else
            {
                DataDir = ExeDir;
            }
            if (ImageFiles.Count == 0)
            {

                foreach (var d in DriveInfo.GetDrives())
                {
                    try { if (d.IsReady) ImageFiles.AddRange(FindImages(d.RootDirectory.FullName)); }
                    catch { }
                }
            }
            LogFile = Path.Combine(Path.GetTempPath(), "pcinstaller.log");
        }

        public static void InitPostSetup(bool demo = false)
        {
            DataDir = ExeDir;
            if (demo && !File.Exists(Path.Combine(ExeDir, "catalog.json")))
            {

                var dir = new DirectoryInfo(ExeDir);
                for (int i = 0; i < 6 && dir != null; i++, dir = dir.Parent)
                {
                    string cand = Path.Combine(dir.FullName, @"usb\installer");
                    if (File.Exists(Path.Combine(cand, "catalog.json"))) { DataDir = cand; break; }
                }
            }
            ProgramsDir = Path.Combine(ExeDir, "Programs");
            LogFile = Path.Combine(ExeDir, @"Logs\postsetup.log");
        }

        static string FindImage(string root) => FindImages(root).FirstOrDefault();

        static List<string> FindImages(string root)
        {
            var res = new List<string>();
            var dirs = new List<string> { root };
            try { dirs.AddRange(Directory.GetDirectories(root).OrderByDescending(d => d)); } catch { }
            foreach (var dir in dirs)
                foreach (var n in new[] { @"sources\install.wim", @"sources\install.esd" })
                {
                    string p = Path.Combine(dir, n);
                    if (File.Exists(p)) { res.Add(p); break; }
                }
            return res;
        }
    }

    static class Log
    {
        static readonly object L = new object();

        public static void W(string s)
        {
            lock (L)
            {
                try
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(Paths.LogFile));
                    File.AppendAllText(Paths.LogFile, DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss ") + s + Environment.NewLine, Encoding.UTF8);
                }
                catch { }
            }
        }
    }

    static class Proc
    {
        public static Encoding Oem
        {
            get
            {
                try { return Encoding.GetEncoding(CultureInfo.CurrentCulture.TextInfo.OEMCodePage); }
                catch { return Encoding.Default; }
            }
        }

        public static int Run(string file, string args, Action<string> onLine = null, Encoding enc = null,
            Action<Process> onStart = null, Action<string> logTo = null)
        {
            Action<string> log = logTo ?? Log.W;
            log("> " + file + " " + args);
            var psi = new ProcessStartInfo(file, args)
            {
                UseShellExecute = false,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                CreateNoWindow = true,
                StandardOutputEncoding = enc ?? Oem,
                StandardErrorEncoding = enc ?? Oem,
            };
            using (var p = Process.Start(psi))
            {
                onStart?.Invoke(p);
                var errThread = new Thread(() =>
                {
                    string l;
                    while ((l = p.StandardError.ReadLine()) != null)
                    {
                        if (l.Trim().Length == 0) continue;
                        log("  ! " + l);
                        onLine?.Invoke(l);
                    }
                }) { IsBackground = true };
                errThread.Start();

                var sb = new StringBuilder();
                string lastLogged = null;
                void Emit()
                {
                    if (sb.Length == 0) return;
                    string line = sb.ToString();
                    sb.Clear();
                    if (line.Trim().Length == 0) return;
                    onLine?.Invoke(line);
                    bool progressLine = line.Contains("%") && (line.Contains("[") || line.Contains("█") || line.Contains("▒"));
                    if (!progressLine && line != lastLogged) { log("  " + line); lastLogged = line; }
                }

                int ch;
                var r = p.StandardOutput;
                while ((ch = r.Read()) != -1)
                {
                    if (ch == '\r' || ch == '\n') Emit();
                    else sb.Append((char)ch);
                }
                Emit();
                p.WaitForExit();
                errThread.Join(2000);
                log("  код завершения: " + p.ExitCode);
                return p.ExitCode;
            }
        }

        public static string Capture(string file, string args, out int code, Encoding enc = null)
        {
            var sb = new StringBuilder();
            code = Run(file, args, l => sb.AppendLine(l), enc);
            return sb.ToString();
        }

        public static void KillTree(Process p)
        {
            try
            {
                if (p == null || p.HasExited) return;
                var k = Process.Start(new ProcessStartInfo("taskkill.exe", "/PID " + p.Id + " /T /F") { CreateNoWindow = true, UseShellExecute = false });
                k?.WaitForExit(5000);
            }
            catch { }
        }

        public static void Shell(string fileOrUrl, string args = null)
        {
            try { Process.Start(new ProcessStartInfo(fileOrUrl, args ?? "") { UseShellExecute = true }); }
            catch (Exception ex) { Log.W("Не удалось открыть " + fileOrUrl + ": " + ex.Message); }
        }
    }

    static class Fmt
    {
        public static string Gb(long bytes)
        {
            double gb = bytes / 1073741824.0;
            if (gb >= 10) return gb.ToString("0", CultureInfo.InvariantCulture) + " ГБ";
            if (gb >= 1) return gb.ToString("0.0", CultureInfo.InvariantCulture) + " ГБ";
            return (bytes / 1048576.0).ToString("0", CultureInfo.InvariantCulture) + " МБ";
        }

        public static double ParseDouble(string s)
        {
            double.TryParse(s.Replace(',', '.'), NumberStyles.Float, CultureInfo.InvariantCulture, out double v);
            return v;
        }
    }

    static class Ext
    {
        public static string Str(this Dictionary<string, object> d, string key, string def = "")
            => d != null && d.TryGetValue(key, out var v) && v != null ? Convert.ToString(v, CultureInfo.InvariantCulture) : def;

        public static bool Bool(this Dictionary<string, object> d, string key, bool def = false)
            => d != null && d.TryGetValue(key, out var v) && v is bool b ? b : def;

        public static int Int(this Dictionary<string, object> d, string key, int def = 0)
            => d != null && d.TryGetValue(key, out var v) && v is double n ? (int)n : def;

        public static List<object> List(this Dictionary<string, object> d, string key)
            => d != null && d.TryGetValue(key, out var v) && v is List<object> l ? l : new List<object>();

        public static List<string> Strings(this Dictionary<string, object> d, string key)
            => d.List(key).Where(x => x != null).Select(x => Convert.ToString(x, CultureInfo.InvariantCulture)).ToList();
    }
}
