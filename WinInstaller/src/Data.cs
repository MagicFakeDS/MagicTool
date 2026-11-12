using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;

namespace UsbInstaller
{

    static class Json
    {
        public static object Parse(string s)
        {
            int i = 0;
            var v = Value(s, ref i);
            Ws(s, ref i);
            if (i < s.Length) throw new FormatException("Лишние символы в JSON на позиции " + i);
            return v;
        }

        public static Dictionary<string, object> ParseFile(string path)
        {
            if (!File.Exists(path)) return null;
            return Parse(File.ReadAllText(path, Encoding.UTF8)) as Dictionary<string, object>;
        }

        static void Ws(string s, ref int i)
        {
            while (i < s.Length)
            {
                if (char.IsWhiteSpace(s[i]) || s[i] == '﻿') { i++; continue; }
                if (s[i] == '/' && i + 1 < s.Length && s[i + 1] == '/') { while (i < s.Length && s[i] != '\n') i++; continue; }
                break;
            }
        }

        static object Value(string s, ref int i)
        {
            Ws(s, ref i);
            if (i >= s.Length) throw new FormatException("JSON неожиданно закончился");
            char c = s[i];
            if (c == '{') return Obj(s, ref i);
            if (c == '[') return Arr(s, ref i);
            if (c == '"') return Str(s, ref i);
            if (Lit(s, ref i, "true")) return true;
            if (Lit(s, ref i, "false")) return false;
            if (Lit(s, ref i, "null")) return null;
            int st = i;
            while (i < s.Length && "+-0123456789.eE".IndexOf(s[i]) >= 0) i++;
            if (st == i) throw new FormatException("Ошибка JSON на позиции " + i);
            return double.Parse(s.Substring(st, i - st), CultureInfo.InvariantCulture);
        }

        static bool Lit(string s, ref int i, string lit)
        {
            if (string.CompareOrdinal(s, i, lit, 0, lit.Length) != 0) return false;
            i += lit.Length;
            return true;
        }

        static Dictionary<string, object> Obj(string s, ref int i)
        {
            var d = new Dictionary<string, object>(StringComparer.OrdinalIgnoreCase);
            i++;
            while (true)
            {
                Ws(s, ref i);
                if (i < s.Length && s[i] == '}') { i++; return d; }
                string k = Str(s, ref i);
                Ws(s, ref i);
                if (i >= s.Length || s[i] != ':') throw new FormatException("Ожидалось ':' на позиции " + i);
                i++;
                d[k] = Value(s, ref i);
                Ws(s, ref i);
                if (i < s.Length && s[i] == ',') { i++; continue; }
                if (i < s.Length && s[i] == '}') { i++; return d; }
                throw new FormatException("Ожидалось ',' или '}' на позиции " + i);
            }
        }

        static List<object> Arr(string s, ref int i)
        {
            var l = new List<object>();
            i++;
            while (true)
            {
                Ws(s, ref i);
                if (i < s.Length && s[i] == ']') { i++; return l; }
                l.Add(Value(s, ref i));
                Ws(s, ref i);
                if (i < s.Length && s[i] == ',') { i++; continue; }
                if (i < s.Length && s[i] == ']') { i++; return l; }
                throw new FormatException("Ожидалось ',' или ']' на позиции " + i);
            }
        }

        static string Str(string s, ref int i)
        {
            Ws(s, ref i);
            if (i >= s.Length || s[i] != '"') throw new FormatException("Ожидалась строка на позиции " + i);
            i++;
            var sb = new StringBuilder();
            while (i < s.Length)
            {
                char c = s[i++];
                if (c == '"') return sb.ToString();
                if (c != '\\') { sb.Append(c); continue; }
                char e = s[i++];
                switch (e)
                {
                    case 'n': sb.Append('\n'); break;
                    case 'r': sb.Append('\r'); break;
                    case 't': sb.Append('\t'); break;
                    case 'b': sb.Append('\b'); break;
                    case 'f': sb.Append('\f'); break;
                    case 'u': sb.Append((char)Convert.ToInt32(s.Substring(i, 4), 16)); i += 4; break;
                    case '"': case '\\': case '/': sb.Append(e); break;

                    default: sb.Append('\\').Append(e); break;
                }
            }
            throw new FormatException("Незакрытая строка в JSON");
        }

        public static string Write(object v, int indent = 0)
        {
            var sb = new StringBuilder();
            W(sb, v, indent);
            return sb.ToString();
        }

        static void W(StringBuilder sb, object v, int ind)
        {
            string pad = new string(' ', ind * 2), pad1 = new string(' ', (ind + 1) * 2);
            switch (v)
            {
                case null: sb.Append("null"); break;
                case bool b: sb.Append(b ? "true" : "false"); break;
                case string s: Esc(sb, s); break;
                case int n: sb.Append(n); break;
                case double d: sb.Append(d.ToString(CultureInfo.InvariantCulture)); break;
                case IDictionary<string, object> d:
                    sb.Append("{\n");
                    int k = 0;
                    foreach (var kv in d)
                    {
                        sb.Append(pad1); Esc(sb, kv.Key); sb.Append(": "); W(sb, kv.Value, ind + 1);
                        sb.Append(++k < d.Count ? ",\n" : "\n");
                    }
                    sb.Append(pad).Append('}');
                    break;
                case System.Collections.IEnumerable e:
                    var items = e.Cast<object>().ToList();
                    sb.Append('[');
                    for (int j = 0; j < items.Count; j++) { if (j > 0) sb.Append(", "); W(sb, items[j], ind + 1); }
                    sb.Append(']');
                    break;
                default: Esc(sb, Convert.ToString(v, CultureInfo.InvariantCulture)); break;
            }
        }

        static void Esc(StringBuilder sb, string s)
        {
            sb.Append('"');
            foreach (char c in s)
            {
                if (c == '"' || c == '\\') sb.Append('\\').Append(c);
                else if (c == '\n') sb.Append("\\n");
                else if (c == '\r') sb.Append("\\r");
                else if (c == '\t') sb.Append("\\t");
                else if (c < 32) sb.Append("\\u").Append(((int)c).ToString("x4"));
                else sb.Append(c);
            }
            sb.Append('"');
        }
    }

    class AppConfig
    {
        public string BrandName = "TOOLBOX PC";
        public string Tagline = "GAME EDITION";
        public string BrandId = "ToolboxPC";
        public string Accent = "#14A39A";
        public string Accent2 = "#2F7DF6";
        public string Website = "";
        public string DefaultEdition = "Windows 11 Pro";
        public string Locale = "ru-RU";
        public string InputLocale = "0419:00000419;0409:00000409";
        public string TimeZone = "";
        public int MinDiskGB = 40;
        public bool DarkByDefault;
        public List<string> ConsumerApps = new List<string>();
        public List<string> LiteApps = new List<string>();

        public static AppConfig Load(string path)
        {
            var c = new AppConfig();
            Dictionary<string, object> d = null;
            try { d = Json.ParseFile(path); }
            catch (Exception ex) { Log.W("Ошибка в config.json: " + ex.Message); }
            if (d == null) return c;
            c.BrandName = d.Str("brandName", c.BrandName);
            c.Tagline = d.Str("tagline", c.Tagline);
            c.BrandId = Regex.Replace(d.Str("brandId", c.BrandId), @"[^A-Za-z0-9_\-]", "");
            if (c.BrandId.Length == 0) c.BrandId = "PCInstaller";
            c.Accent = d.Str("accentColor", c.Accent);
            c.Accent2 = d.Str("accentColor2", c.Accent2);
            c.Website = d.Str("website", c.Website);
            c.DefaultEdition = d.Str("defaultEdition", c.DefaultEdition);
            c.Locale = d.Str("locale", c.Locale);
            c.InputLocale = d.Str("inputLocale", c.InputLocale);
            c.TimeZone = d.Str("timeZone", c.TimeZone);
            c.MinDiskGB = d.Int("minDiskGB", c.MinDiskGB);
            c.DarkByDefault = d.Bool("darkByDefault", c.DarkByDefault);
            c.ConsumerApps = d.Strings("consumerApps");
            c.LiteApps = d.Strings("liteExtraApps");
            return c;
        }
    }

    class CatalogItem
    {
        public string Id, Name, Description, Type, Winget, File, Url, Args, Site, Icon, Color, Category;

        public string Exe, Target, Shortcut;
        public bool Default;

        public bool FromUsb => Type == "local" || Type == "drivers" || Type == "portable";
        public bool Available = true;
        public string Missing;

        public List<string> Detect = new List<string>();

        public string DetectLabel;

        public string DetectedDevice;

        public string Source
        {
            get
            {
                switch (Type)
                {
                    case "winget": return "winget · " + Winget;
                    case "url": return "загрузка · " + Url;
                    case "drivers": return "драйверы с флешки · Programs\\" + File;

                    case "portable": return "с флешки, без установки · " + Regex.Replace(Target ?? "", "%SystemDrive%", "C:", RegexOptions.IgnoreCase);
                    case "wudrivers": return "Центр обновления Windows (официальные драйверы производителей)";
                    default: return "с флешки · Programs\\" + File;
                }
            }
        }
    }

    class CatalogCategory
    {
        public string Name;
        public List<CatalogItem> Items = new List<CatalogItem>();
    }

    class Catalog
    {
        public List<CatalogCategory> Categories = new List<CatalogCategory>();
        public IEnumerable<CatalogItem> All => Categories.SelectMany(c => c.Items);
        public CatalogItem Find(string id) => All.FirstOrDefault(i => string.Equals(i.Id, id, StringComparison.OrdinalIgnoreCase));

        public void DetectDevices()
        {
            try
            {
                foreach (var it in All.Where(i => i.Detect.Count > 0))
                {
                    var d = Devices.Match(it.Detect);
                    if (d != null) { it.DetectedDevice = string.IsNullOrEmpty(it.DetectLabel) ? d.Display : it.DetectLabel; Log.W("Найдено устройство для «" + it.Name + "»: " + d.Display); }
                }
                Log.W("Материнская плата: " + Devices.Board);
                Log.W("USB-устройства:\n  " + string.Join("\n  ", Devices.UsbSummary()));
            }
            catch (Exception ex) { Log.W("Автоопределение устройств: " + ex.Message); }
        }

        public static Catalog Load(string path, string programsDir)
        {
            var cat = new Catalog();
            Dictionary<string, object> d = null;
            try { d = Json.ParseFile(path); }
            catch (Exception ex) { Log.W("Ошибка в catalog.json: " + ex.Message); }
            if (d == null) return cat;
            foreach (var co in d.List("categories"))
            {
                var cd = co as Dictionary<string, object>;
                if (cd == null) continue;
                var c = new CatalogCategory { Name = cd.Str("name", "Без названия") };
                foreach (var io in cd.List("items"))
                {
                    var id = io as Dictionary<string, object>;
                    if (id == null) continue;
                    var it = new CatalogItem
                    {
                        Id = id.Str("id"),
                        Name = id.Str("name"),
                        Description = id.Str("description"),
                        Type = id.Str("type", "winget").ToLowerInvariant(),
                        Winget = id.Str("winget"),
                        File = id.Str("file"),
                        Url = id.Str("url"),
                        Args = id.Str("args"),
                        Exe = id.Str("exe"),
                        Target = id.Str("target"),
                        Shortcut = id.Str("shortcut"),
                        Site = id.Str("site"),
                        Icon = id.Str("icon"),
                        Color = id.Str("color"),
                        Default = id.Bool("default"),
                        Category = c.Name,
                        Detect = id.Strings("detect"),
                        DetectLabel = id.Str("detectLabel", null),
                    };
                    if (it.Id.Length == 0) it.Id = (it.Winget.Length > 0 ? it.Winget : it.Name).ToLowerInvariant();
                    if (programsDir != null && it.FromUsb)
                    {
                        string p = Path.Combine(programsDir, it.File ?? "");
                        if (it.Type == "portable") p = Path.Combine(p, it.Exe ?? "");
                        if (string.IsNullOrEmpty(it.File) || !(System.IO.File.Exists(p) || Directory.Exists(p)))
                        {
                            it.Available = false;
                            it.Missing = "Нет на флешке: Programs\\" + it.File;
                        }
                    }
                    c.Items.Add(it);
                }
                if (c.Items.Count > 0) cat.Categories.Add(c);
            }
            return cat;
        }
    }

    class TweakDef
    {
        public string Id, Title, Desc;
        public bool Risky, Default, Post;
    }

    static class Tweaks
    {
        public static readonly List<TweakDef> All = new List<TweakDef>
        {
            T("pauseUpdates", "Пауза обновлений на 35 дней", "Временно приостанавливает обновления Windows. Снижает защищённость до возобновления — включите обновления вручную позже.", risky: true),
            T("highPerf", "Высокая производительность", "Включает схему электропитания «Высокая производительность». На ноутбуках заметно сокращает время работы от батареи.", risky: true, post: true),
            T("noSleepAC", "Не выключать экран и сон от сети", "Для стационарного ПК: экран и сон не отключаются при питании от сети.", def: true, post: true),
            T("removeConsumerApps", "Удалить потребительские приложения", "Удаляет Clipchamp, новости, погоду, советы, Solitaire, Teams и другие предустановленные приложения. Store и системные компоненты сохраняются.", def: true),
            T("removeOneDrive", "Удалить OneDrive", "Клиент OneDrive не будет устанавливаться и запускаться. Файлы в облаке не затрагиваются.", def: true, post: true),
            T("uacNever", "UAC: «Никогда не уведомлять»", "Отключает запросы контроля учётных записей для администраторов. Снижает защищённость системы.", risky: true),
            T("localAccount", "Локальная учётная запись", "Позволяет пройти первую настройку без учётной записи Microsoft и без интернета.", def: true),
            T("showExtensions", "Показывать расширения файлов", "В проводнике будут видны расширения: .exe, .jpg, .txt и т.д.", def: true, post: true),
            T("noTelemetry", "Минимальная телеметрия", "Отключает службу сбора данных DiagTrack и рекламный идентификатор.", def: true),
            T("noWidgets", "Отключить виджеты и Copilot", "Убирает ленту новостей, виджеты и кнопку Copilot с панели задач.", def: false),
            T("classicMenu", "Классическое контекстное меню", "Возвращает полное контекстное меню Windows 10 по правому клику без «Показать дополнительные параметры».", post: true),
            T("darkMode", "Тёмная тема Windows", "Включает тёмное оформление системы и приложений.", post: true),
        };

        static TweakDef T(string id, string title, string desc, bool risky = false, bool def = false, bool post = false)
            => new TweakDef { Id = id, Title = title, Desc = desc, Risky = risky, Default = def, Post = post };

        public static bool IsPost(string id) => All.Any(t => t.Id == id && t.Post);
    }

    class DiskTarget
    {
        public int Disk;
        public string DiskName;
        public long DiskSize;
        public string DiskStyle;
        public bool WholeDisk;
        public bool FreeSpace;
        public int Partition;
        public long Offset, Size;

        public bool KeepData;
        public string Root;

        public string Describe()
        {
            if (KeepData) return "диск " + Root + " — старая Windows будет перенесена в Windows.old, файлы пользователей перенесутся в новую систему";
            string d = "Диск " + Disk + " · " + DiskName + " (" + Fmt.Gb(DiskSize) + ")";
            if (WholeDisk) return d + " — весь диск, все данные будут удалены";
            if (FreeSpace) return d + " — незанятое пространство " + Fmt.Gb(Size);
            return d + " — раздел " + Partition + " (" + Fmt.Gb(Size) + "), будет отформатирован";
        }
    }

    class InstallPlan
    {
        public int ImageIndex = 1;
        public string ImageName = "";
        public string ImageFile;
        public string Os = "Windows 11";
        public bool Lite;
        public DiskTarget Target;
        public HashSet<string> Apps = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        public HashSet<string> Tweaks = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        public string UserName = "";

        public List<string> MigrateUsers = new List<string>();
    }

    class WimImage
    {
        public int Index;
        public string Name;
        public long Size;
        public string File;
        public int Build;
        public string EditionId = "";

        public string Os => Build >= 22000 ? "Windows 11" : Build > 0 ? "Windows 10" : (Name != null && Name.Contains("10") ? "Windows 10" : "Windows 11");
    }

    static class Wim
    {
        public static List<WimImage> GetImages(string file)
        {
            List<WimImage> list = null;
            try { list = ReadXml(file); }
            catch (Exception ex) { Log.W("XML образа не прочитан: " + ex.Message); }
            if (list == null || list.Count == 0) list = FromDism(file);
            foreach (var i in list) i.File = file;
            return list;
        }

        public static List<WimImage> GetAll(IEnumerable<string> files)
        {
            var all = new List<WimImage>();
            foreach (var f in files)
            {
                try { all.AddRange(GetImages(f)); }
                catch (Exception ex) { Log.W("Образ " + f + ": " + ex.Message); }
            }

            return all.OrderByDescending(i => i.Os).ThenBy(i => i.File).ThenBy(i => i.Index).ToList();
        }

        static List<WimImage> ReadXml(string file)
        {
            var list = new List<WimImage>();
            using (var fs = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            using (var br = new BinaryReader(fs))
            {
                if (Encoding.ASCII.GetString(br.ReadBytes(8)) != "MSWIM\0\0\0") return list;
                fs.Position = 72;
                long packed = br.ReadInt64() & 0x00FFFFFFFFFFFFFFL;
                long offset = br.ReadInt64();
                if (packed <= 0 || packed > 64 * 1024 * 1024 || offset <= 0) return list;
                fs.Position = offset;
                string xml = Encoding.Unicode.GetString(br.ReadBytes((int)packed)).TrimStart('﻿').TrimEnd('\0');
                var doc = new System.Xml.XmlDocument();
                doc.LoadXml(xml);
                foreach (System.Xml.XmlNode n in doc.SelectNodes("/WIM/IMAGE"))
                {
                    var img = new WimImage { Index = int.Parse(n.Attributes["INDEX"].Value, CultureInfo.InvariantCulture) };
                    img.Name = (n.SelectSingleNode("DISPLAYNAME") ?? n.SelectSingleNode("NAME"))?.InnerText ?? ("Образ " + img.Index);
                    long.TryParse(n.SelectSingleNode("TOTALBYTES")?.InnerText, out img.Size);
                    int.TryParse(n.SelectSingleNode("WINDOWS/VERSION/BUILD")?.InnerText, out img.Build);
                    img.EditionId = n.SelectSingleNode("WINDOWS/EDITIONID")?.InnerText ?? "";
                    list.Add(img);
                }
            }
            return list.OrderBy(i => i.Index).ToList();
        }

        static List<WimImage> FromDism(string file)
        {
            var list = new List<WimImage>();
            string o = Proc.Capture("dism.exe", "/English /Get-WimInfo /WimFile:\"" + file + "\"", out int code);
            WimImage cur = null;
            foreach (var raw in o.Split('\n'))
            {
                string line = raw.Trim();
                int c = line.IndexOf(':');
                if (c < 0) continue;
                string k = line.Substring(0, c).Trim(), v = line.Substring(c + 1).Trim();
                if (k == "Index") { cur = new WimImage { Index = int.Parse(v, CultureInfo.InvariantCulture) }; list.Add(cur); }
                else if (cur != null && k == "Name") cur.Name = v;
                else if (cur != null && k == "Size") long.TryParse(Regex.Replace(v, @"[^\d]", ""), out cur.Size);
            }
            if (code != 0) Log.W("Не удалось прочитать образ: код " + code);
            return list;
        }
    }
}
