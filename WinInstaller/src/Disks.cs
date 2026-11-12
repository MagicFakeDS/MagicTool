using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Management;
using System.Text;

namespace UsbInstaller
{
    class PartInfo
    {
        public int Number;
        public long Offset, Size, FreeBytes = -1;
        public char Letter;
        public string Label = "", FileSystem = "", GptType = "";
        public int MbrType;
        public bool IsActive;

        public bool IsEsp => GptType.Equals("{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}", StringComparison.OrdinalIgnoreCase) || MbrType == 0xEF;
        public bool IsMsr => GptType.Equals("{e3c9e316-0b5c-4db8-817d-f92df00215ae}", StringComparison.OrdinalIgnoreCase);
        public bool IsRecovery => GptType.Equals("{de94bba4-06d1-4d40-a16a-bfd50179d6ac}", StringComparison.OrdinalIgnoreCase) || MbrType == 0x27;
        public bool IsService => IsEsp || IsMsr || IsRecovery;

        public string Kind => IsEsp ? "Системный (EFI)" : IsMsr ? "Зарезервировано (MSR)" : IsRecovery ? "Восстановление" : "Основной";
    }

    class Extent
    {
        public long Offset, Size;
    }

    class DiskInfo
    {
        public int Number;
        public string Name = "", Bus = "", Style = "RAW";
        public long Size;

        public bool IsInstaller, IsOffline, IsReadOnly;
        public string ProtectReason = "установочная флешка";
        public bool Detailed = true;
        public List<PartInfo> Parts = new List<PartInfo>();
        public List<Extent> Free = new List<Extent>();

        public string Title => "Диск " + Number + " · " + Name;

        public void ComputeFree()
        {
            Free.Clear();
            const long MB = 1048576;
            long start = MB, end = Size - (Style == "GPT" ? MB : 0);
            foreach (var p in Parts.OrderBy(p => p.Offset))
            {
                if (p.Offset - start >= 64 * MB) Free.Add(new Extent { Offset = Align(start), Size = p.Offset - Align(start) });
                start = Math.Max(start, p.Offset + p.Size);
            }
            if (end - start >= 64 * MB) Free.Add(new Extent { Offset = Align(start), Size = end - Align(start) });
        }

        static long Align(long v) => (v + 1048575) / 1048576 * 1048576;
    }

    static class DiskManager
    {
        static readonly Dictionary<int, string> Buses = new Dictionary<int, string>
        {
            { 1, "SCSI" }, { 3, "ATA" }, { 6, "Fibre Channel" }, { 7, "USB" }, { 8, "RAID" }, { 9, "iSCSI" }, { 10, "SAS" },
            { 11, "SATA" }, { 12, "SD" }, { 13, "MMC" }, { 15, "Virtual" }, { 17, "NVMe" },
        };

        public static List<DiskInfo> Load()
        {
            List<DiskInfo> list;
            try { list = LoadStorage(); }
            catch (Exception ex)
            {
                Log.W("Storage WMI недоступен (" + ex.Message + "), пробуем Win32_DiskDrive");
                list = LoadWin32();
            }

            bool atRoot = Paths.UsbRoot != null && Paths.UsbRoot.TrimEnd('\\').Length == 2 && Paths.UsbRoot[1] == ':';
            char usb = atRoot ? char.ToUpperInvariant(Paths.UsbRoot[0]) : '\0';
            var imageLetters = new HashSet<char>(Paths.ImageFiles.Where(f => f.Length > 1 && f[1] == ':').Select(f => char.ToUpperInvariant(f[0])));
            foreach (var d in list)
            {
                if (usb != '\0' && d.Parts.Any(p => char.ToUpperInvariant(p.Letter) == usb)) d.IsInstaller = true;

                else if (d.Parts.Any(p => p.Letter != '\0' && imageLetters.Contains(char.ToUpperInvariant(p.Letter))))
                {
                    d.IsInstaller = true;
                    d.ProtectReason = "здесь лежит образ Windows";
                }
                d.Parts.Sort((a, b) => a.Offset.CompareTo(b.Offset));
                if (d.Detailed) d.ComputeFree();
            }
            list.Sort((a, b) => a.Number.CompareTo(b.Number));
            return list;
        }

        static ManagementObjectCollection Q(ManagementScope s, string q) => new ManagementObjectSearcher(s, new ObjectQuery(q)).Get();

        static List<DiskInfo> LoadStorage()
        {
            var scope = new ManagementScope(@"\\.\root\Microsoft\Windows\Storage");
            scope.Connect();
            var vols = new Dictionary<string, ManagementBaseObject>(StringComparer.OrdinalIgnoreCase);
            foreach (ManagementObject v in Q(scope, "SELECT * FROM MSFT_Volume"))
            {
                var path = v["Path"] as string;
                if (path != null) vols[path] = v;
            }
            var disks = new List<DiskInfo>();
            foreach (ManagementObject d in Q(scope, "SELECT * FROM MSFT_Disk"))
            {
                int style = Convert.ToInt32(d["PartitionStyle"] ?? 0);
                int bus = Convert.ToInt32(d["BusType"] ?? 0);
                disks.Add(new DiskInfo
                {
                    Number = Convert.ToInt32(d["Number"]),
                    Name = ((d["FriendlyName"] ?? d["Model"]) as string ?? "Диск").Trim(),
                    Size = Convert.ToInt64(d["Size"] ?? 0L),
                    Style = style == 1 ? "MBR" : style == 2 ? "GPT" : "RAW",
                    Bus = Buses.TryGetValue(bus, out var b) ? b : "",
                    IsOffline = d["IsOffline"] is bool off && off,
                    IsReadOnly = d["IsReadOnly"] is bool ro && ro,
                });
            }
            foreach (ManagementObject p in Q(scope, "SELECT * FROM MSFT_Partition"))
            {
                int dn = Convert.ToInt32(p["DiskNumber"]);
                var disk = disks.FirstOrDefault(x => x.Number == dn);
                if (disk == null) continue;
                var pi = new PartInfo
                {
                    Number = Convert.ToInt32(p["PartitionNumber"]),
                    Offset = Convert.ToInt64(p["Offset"] ?? 0L),
                    Size = Convert.ToInt64(p["Size"] ?? 0L),
                    GptType = p["GptType"] as string ?? "",
                    MbrType = p["MbrType"] == null ? 0 : Convert.ToInt32(p["MbrType"]),
                    IsActive = p["IsActive"] is bool a && a,
                };
                try
                {
                    var dl = p["DriveLetter"];
                    if (dl != null) { char c = Convert.ToChar(dl); if (char.IsLetter(c)) pi.Letter = char.ToUpperInvariant(c); }
                }
                catch { }
                if (p["AccessPaths"] is string[] paths)
                    foreach (var ap in paths)
                        if (vols.TryGetValue(ap, out var v))
                        {
                            pi.FileSystem = v["FileSystem"] as string ?? "";
                            pi.Label = v["FileSystemLabel"] as string ?? "";
                            if (v["SizeRemaining"] != null) pi.FreeBytes = Convert.ToInt64(v["SizeRemaining"]);
                        }
                disk.Parts.Add(pi);
            }
            return disks;
        }

        static List<DiskInfo> LoadWin32()
        {
            var list = new List<DiskInfo>();
            try
            {
                foreach (ManagementObject d in new ManagementObjectSearcher("SELECT * FROM Win32_DiskDrive").Get())
                {
                    list.Add(new DiskInfo
                    {
                        Number = Convert.ToInt32(d["Index"]),
                        Name = (d["Model"] as string ?? "Диск").Trim(),
                        Size = Convert.ToInt64(d["Size"] ?? 0L),
                        Bus = d["InterfaceType"] as string ?? "",
                        Style = "?",
                        Detailed = false,
                    });
                }
            }
            catch (Exception ex) { Log.W("Win32_DiskDrive: " + ex.Message); }
            return list;
        }
    }

    class DiskOpException : Exception
    {
        public DiskOpException(string m) : base(m) { }
    }

    static class DiskOps
    {
        const long MB = 1048576;

        public static void RunDiskpart(string script)
        {
            Log.W("diskpart:\n" + script);
            if (Env.Demo) { System.Threading.Thread.Sleep(600); return; }
            string f = Path.Combine(Path.GetTempPath(), "dp_" + Guid.NewGuid().ToString("N") + ".txt");
            File.WriteAllText(f, script, Encoding.ASCII);
            try
            {
                int code = Proc.Run("diskpart.exe", "/s \"" + f + "\"");
                if (code != 0) throw new DiskOpException("diskpart завершился с ошибкой (код " + code + "). Подробности в журнале.");
            }
            finally { try { File.Delete(f); } catch { } }
        }

        static string Head(int disk) => "select disk " + disk + "\r\nonline disk noerr\r\nattributes disk clear readonly noerr\r\n";

        public static void DeletePartition(int disk, int part)
            => RunDiskpart(Head(disk) + "select partition " + part + "\r\ndelete partition override\r\n");

        public static void FormatPartition(int disk, int part)
            => RunDiskpart(Head(disk) + "select partition " + part + "\r\nformat quick fs=ntfs label=\"Data\"\r\n");

        public static void WipeDisk(int disk)
            => RunDiskpart(Head(disk) + "clean\r\nconvert " + (Env.IsUefi ? "gpt" : "mbr") + "\r\n");

        public static void CreatePartition(DiskInfo d, Extent e)
        {
            var sb = new StringBuilder(Head(d.Number));
            if (d.Style == "RAW") sb.Append("convert " + (Env.IsUefi ? "gpt" : "mbr") + "\r\n");
            sb.Append("create partition primary offset=" + e.Offset / 1024 + " size=" + (e.Size / MB - 1) + "\r\n");
            sb.Append("format quick fs=ntfs label=\"Data\"\r\nassign\r\n");
            RunDiskpart(sb.ToString());
        }

        public static char FreeLetter(char preferred, params char[] exclude)
        {
            var used = new HashSet<char>(DriveInfo.GetDrives().Select(x => char.ToUpperInvariant(x.Name[0])));
            foreach (var c in exclude) used.Add(c);
            if (!used.Contains(preferred)) return preferred;
            for (char c = 'Z'; c >= 'D'; c--) if (!used.Contains(c)) return c;
            throw new DiskOpException("Нет свободных букв дисков.");
        }

        public static void PrepareForInstall(DiskTarget t, bool uefi, out char win, out char sys)
        {
            win = FreeLetter('W');
            sys = FreeLetter('S', win);
            var disks = DiskManager.Load();
            var d = disks.FirstOrDefault(x => x.Number == t.Disk) ?? throw new DiskOpException("Диск " + t.Disk + " не найден.");
            if (d.IsInstaller) throw new DiskOpException("Нельзя устанавливать на установочную флешку.");
            var sb = new StringBuilder(Head(d.Number));
            bool whole = t.WholeDisk || d.Style == "RAW";

            if (whole)
            {
                sb.Append("clean\r\n");
                if (uefi)
                {
                    sb.Append("convert gpt\r\n");
                    sb.Append("create partition efi size=260\r\nformat quick fs=fat32 label=\"System\"\r\nassign letter=" + sys + "\r\n");
                    sb.Append("create partition msr size=16\r\n");
                    sb.Append("create partition primary\r\nformat quick fs=ntfs label=\"Windows\"\r\nassign letter=" + win + "\r\n");
                }
                else
                {
                    sb.Append("convert mbr\r\n");
                    sb.Append("create partition primary size=100\r\nformat quick fs=ntfs label=\"System Reserved\"\r\nactive\r\nassign letter=" + sys + "\r\n");
                    sb.Append("create partition primary\r\nformat quick fs=ntfs label=\"Windows\"\r\nassign letter=" + win + "\r\n");
                }
                RunDiskpart(sb.ToString());
                return;
            }

            if (uefi && d.Style != "GPT") throw new DiskOpException("Компьютер загружен в режиме UEFI, а диск размечен как MBR. Выберите «Весь диск», чтобы переразметить его в GPT.");
            if (!uefi && d.Style != "MBR") throw new DiskOpException("Компьютер загружен в режиме BIOS (Legacy), а диск размечен как GPT. Выберите «Весь диск».");

            PartInfo esp = null;
            DiskInfo espDisk = null;
            if (uefi)
            {
                esp = d.Parts.FirstOrDefault(p => p.IsEsp);
                espDisk = esp != null ? d : null;
                if (esp == null && !t.FreeSpace)
                    foreach (var od in disks)
                    {
                        var e2 = od.Parts.FirstOrDefault(p => p.IsEsp);
                        if (e2 != null && !od.IsInstaller) { esp = e2; espDisk = od; break; }
                    }
                if (esp == null && !t.FreeSpace) throw new DiskOpException("Не найден системный раздел EFI. Выберите незанятое пространство или «Весь диск».");
                if (esp != null)
                {

                    if (esp.Letter != '\0') sys = esp.Letter;
                    else
                    {
                        var s2 = new StringBuilder(Head(espDisk.Number));
                        s2.Append("select partition " + esp.Number + "\r\nassign letter=" + sys + "\r\n");
                        RunDiskpart(s2.ToString());
                    }
                }
            }

            if (t.FreeSpace)
            {
                long off = t.Offset, end = t.Offset + t.Size;
                if (uefi && esp == null)
                {
                    sb.Append("create partition efi size=260 offset=" + off / 1024 + "\r\nformat quick fs=fat32 label=\"System\"\r\nassign letter=" + sys + "\r\n");
                    off += 260 * MB;
                    sb.Append("create partition msr size=16 offset=" + off / 1024 + "\r\n");
                    off += 16 * MB;
                }
                sb.Append("create partition primary offset=" + off / 1024 + " size=" + ((end - off) / MB - 1) + "\r\n");
                sb.Append("format quick fs=ntfs label=\"Windows\"\r\nassign letter=" + win + "\r\n");
                if (!uefi) { sb.Append("active\r\n"); sys = win; }
            }
            else
            {
                var part = d.Parts.FirstOrDefault(p => p.Number == t.Partition) ?? throw new DiskOpException("Раздел " + t.Partition + " не найден.");
                if (part.IsService) throw new DiskOpException("Нельзя установить Windows на служебный раздел.");
                sb.Append("select partition " + part.Number + "\r\nformat quick fs=ntfs label=\"Windows\" override\r\n");
                if (part.Letter != '\0') win = part.Letter;
                else sb.Append("assign letter=" + win + "\r\n");
                if (!uefi) { sb.Append("active\r\n"); sys = win; }
            }
            RunDiskpart(sb.ToString());
        }
    }
}
