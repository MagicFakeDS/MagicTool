using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace UsbInstaller
{

    static class Devices
    {
        public class Dev
        {
            public string Name = "", BusName = "", Ids = "";
            public string Display => BusName.Length > 0 ? BusName : Name;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct SP_DEVINFO_DATA { public int cbSize; public Guid ClassGuid; public int DevInst; public IntPtr Reserved; }

        [StructLayout(LayoutKind.Sequential)]
        struct DEVPROPKEY { public Guid fmtid; public int pid; }

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr SetupDiGetClassDevsW(IntPtr classGuid, string enumerator, IntPtr hwnd, int flags);

        [DllImport("setupapi.dll", SetLastError = true)]
        static extern bool SetupDiEnumDeviceInfo(IntPtr set, int index, ref SP_DEVINFO_DATA data);

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool SetupDiGetDeviceRegistryPropertyW(IntPtr set, ref SP_DEVINFO_DATA data, int prop, out int regType, byte[] buf, int size, out int required);

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool SetupDiGetDevicePropertyW(IntPtr set, ref SP_DEVINFO_DATA data, ref DEVPROPKEY key, out int type, byte[] buf, int size, out int required, int flags);

        [DllImport("setupapi.dll")]
        static extern bool SetupDiDestroyDeviceInfoList(IntPtr set);

        const int DIGCF_PRESENT = 2, DIGCF_ALLCLASSES = 4;
        const int SPDRP_DEVICEDESC = 0, SPDRP_HARDWAREID = 1, SPDRP_COMPATIBLEIDS = 2, SPDRP_FRIENDLYNAME = 12;
        static DEVPROPKEY BusReportedDesc = new DEVPROPKEY { fmtid = new Guid("540b947e-8b40-45bc-a8a2-6a0b894cbda2"), pid = 4 };

        static List<Dev> cache;

        public static List<Dev> List()
        {
            if (cache != null) return cache;
            var list = new List<Dev>();
            IntPtr set = SetupDiGetClassDevsW(IntPtr.Zero, null, IntPtr.Zero, DIGCF_PRESENT | DIGCF_ALLCLASSES);
            if (set == IntPtr.Zero || set == new IntPtr(-1)) return cache = list;
            try
            {
                var data = new SP_DEVINFO_DATA { cbSize = Marshal.SizeOf(typeof(SP_DEVINFO_DATA)) };
                for (int i = 0; SetupDiEnumDeviceInfo(set, i, ref data); i++)
                {
                    var d = new Dev
                    {

                        Ids = string.Join("|", RegMulti(set, ref data, SPDRP_HARDWAREID).Concat(RegMulti(set, ref data, SPDRP_COMPATIBLEIDS))).ToUpperInvariant(),
                        Name = RegString(set, ref data, SPDRP_FRIENDLYNAME) ?? RegString(set, ref data, SPDRP_DEVICEDESC) ?? "",
                        BusName = PropString(set, ref data, BusReportedDesc) ?? "",
                    };
                    if (d.Ids.Length > 0) list.Add(d);
                }
            }
            catch (Exception ex) { Log.W("Список устройств: " + ex.Message); }
            finally { SetupDiDestroyDeviceInfoList(set); }
            return cache = list;
        }

        static byte[] Raw(IntPtr set, ref SP_DEVINFO_DATA data, int prop)
        {
            SetupDiGetDeviceRegistryPropertyW(set, ref data, prop, out _, null, 0, out int req);
            if (req <= 0) return null;
            var buf = new byte[req];
            return SetupDiGetDeviceRegistryPropertyW(set, ref data, prop, out _, buf, buf.Length, out _) ? buf : null;
        }

        static string RegString(IntPtr set, ref SP_DEVINFO_DATA data, int prop)
        {
            var b = Raw(set, ref data, prop);
            return b == null ? null : Encoding.Unicode.GetString(b).TrimEnd('\0');
        }

        static IEnumerable<string> RegMulti(IntPtr set, ref SP_DEVINFO_DATA data, int prop)
        {
            var b = Raw(set, ref data, prop);
            return b == null ? new string[0] : Encoding.Unicode.GetString(b).Split(new[] { '\0' }, StringSplitOptions.RemoveEmptyEntries);
        }

        static string PropString(IntPtr set, ref SP_DEVINFO_DATA data, DEVPROPKEY key)
        {
            SetupDiGetDevicePropertyW(set, ref data, ref key, out _, null, 0, out int req, 0);
            if (req <= 0) return null;
            var buf = new byte[req];
            return SetupDiGetDevicePropertyW(set, ref data, ref key, out _, buf, buf.Length, out _, 0)
                ? Encoding.Unicode.GetString(buf).TrimEnd('\0') : null;
        }

        public static Dev Match(IEnumerable<string> rules)
        {
            foreach (var rule in rules)
            {
                var parts = rule.Split('+').Select(p => p.Trim()).Where(p => p.Length > 0).ToArray();
                if (parts.Length == 0) continue;

                var boardParts = parts.Where(p => p.StartsWith("board:", StringComparison.OrdinalIgnoreCase)).ToArray();
                if (boardParts.Length > 0)
                {
                    if (!boardParts.All(p => Contains(Board, p.Substring(6)))) continue;
                    parts = parts.Except(boardParts).ToArray();
                    if (parts.Length == 0) return new Dev { BusName = "плата " + Board };
                }
                var hits = List().Where(d => parts.All(p => p.StartsWith("name:", StringComparison.OrdinalIgnoreCase)
                    ? Contains(d.Name, p.Substring(5)) || Contains(d.BusName, p.Substring(5))
                    : Contains(d.Ids, p))).ToList();
                if (hits.Count == 0) continue;

                var vidpid = VidPid(hits[0].Ids);
                var all = vidpid == null ? hits : List().Where(d => d.Ids.Contains(vidpid)).ToList();
                var best = all.OrderBy(Generic).First();
                string name = best.Display;
                string vendor = Vendor(vidpid);
                if (vendor != null && name.IndexOf(vendor, StringComparison.OrdinalIgnoreCase) < 0) name = vendor + " " + name;
                return new Dev { Name = best.Name, BusName = name, Ids = best.Ids };
            }
            return null;
        }

        static string board;

        public static string Board
        {
            get
            {
                if (board != null) return board;
                board = "";
                try
                {
                    foreach (System.Management.ManagementObject b in new System.Management.ManagementObjectSearcher("SELECT Manufacturer, Product FROM Win32_BaseBoard").Get())
                        board = ((b["Manufacturer"] as string ?? "") + " " + (b["Product"] as string ?? "")).Trim();
                }
                catch (Exception ex) { Log.W("Плата: " + ex.Message); }
                return board;
            }
        }

        static string VidPid(string ids)
        {
            int v = ids.IndexOf("VID_");
            return v >= 0 && ids.Length >= v + 17 ? ids.Substring(v, 17) : null;
        }

        static int Generic(Dev d)
        {
            string s = d.Display;
            if (s.IndexOf("HID", StringComparison.OrdinalIgnoreCase) >= 0 || s.IndexOf("USB-устройство", StringComparison.OrdinalIgnoreCase) >= 0
                || s.IndexOf("Составное", StringComparison.OrdinalIgnoreCase) >= 0 || s.IndexOf("Composite", StringComparison.OrdinalIgnoreCase) >= 0) return 2;
            return d.BusName.Length > 0 ? 0 : 1;
        }

        static string Vendor(string vidpid)
        {
            if (vidpid == null) return null;
            switch (vidpid.Substring(4, 4))
            {
                case "046D": return "Logitech";
                case "1532": return "Razer";
                case "1B1C": return "Corsair";
                case "1038": return "SteelSeries";
                case "0951": return "HyperX";
                case "3434": return "Keychron";
                default: return null;
            }
        }

        static bool Contains(string s, string what) => s != null && s.IndexOf(what.Trim(), StringComparison.OrdinalIgnoreCase) >= 0;

        public static List<string> UsbSummary()
        {
            var seen = new HashSet<string>();
            var res = new List<string>();
            foreach (var d in List())
            {
                var id = d.Ids.Split('|').FirstOrDefault(x => x.StartsWith("USB\\VID_") || x.StartsWith("HID\\VID_"));
                if (id == null) continue;
                int v = id.IndexOf("VID_");
                string vidpid = id.Substring(v, Math.Min(17, id.Length - v));
                string line = vidpid + "  " + d.Display;
                if (seen.Add(line)) res.Add(line);
            }
            return res;
        }
    }
}
