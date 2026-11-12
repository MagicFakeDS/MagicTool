using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Windows.Forms;

namespace UsbInstaller
{
    class Palette
    {
        public Color Bg1, Bg2, Card, Surface, SurfaceHover, Border, Text, Muted, Accent, Accent2, AccentHover, AccentSoft, AccentSoft2, AccentLine, OnAccent, Danger, DangerSoft, Shadow;
    }

    static class Theme
    {
        public static bool Dark;
        public static Color AccentBase = Color.FromArgb(0x14, 0xA3, 0x9A);

        public static Color Accent2Base = Color.FromArgb(0x2F, 0x7D, 0xF6);
        static Palette light, dark;

        public static Palette P => Dark ? dark : light;

        public static void SetAccent2(string hex)
        {
            try { if (!string.IsNullOrEmpty(hex)) Accent2Base = ColorTranslator.FromHtml(hex); } catch { }
        }

        public static void Init(string accentHex, bool isDark)
        {
            try { if (!string.IsNullOrEmpty(accentHex)) AccentBase = ColorTranslator.FromHtml(accentHex); } catch { }
            Dark = isDark;
            var a = AccentBase;
            var a2 = Accent2Base;
            light = new Palette
            {
                Bg1 = Hex("#EEF2F5"), Bg2 = Hex("#D5DDE4"),
                Card = Color.White, Surface = Hex("#F6F8FA"), SurfaceHover = Hex("#EDF1F4"),
                Border = Hex("#DCE2E8"), Text = Hex("#18202A"), Muted = Hex("#6A7684"),
                Accent = a, Accent2 = a2, AccentHover = Blend(a, Color.Black, 0.88), AccentSoft = Blend(a, Color.White, 0.10), AccentSoft2 = Blend(a2, Color.White, 0.09),
                AccentLine = Blend(a, Color.White, 0.55), OnAccent = Color.White,
                Danger = Hex("#E0484D"), DangerSoft = Hex("#FDEDED"), Shadow = Color.FromArgb(26, 20, 30, 40),
            };
            var card = Hex("#171D24");
            dark = new Palette
            {
                Bg1 = Hex("#0A0E13"), Bg2 = Hex("#151C25"),
                Card = card, Surface = Hex("#1F262E"), SurfaceHover = Hex("#272F39"),
                Border = Hex("#323C47"), Text = Hex("#E8EDF2"), Muted = Hex("#8F9BA8"),
                Accent = a, Accent2 = a2, AccentHover = Blend(a, Color.White, 0.88), AccentSoft = Blend(a, card, 0.30), AccentSoft2 = Blend(a2, card, 0.28),
                AccentLine = Blend(a, card, 0.75), OnAccent = Color.White,
                Danger = Hex("#F0565B"), DangerSoft = Blend(Hex("#F0565B"), card, 0.16), Shadow = Color.FromArgb(70, 0, 0, 0),
            };
        }

        public static Color Hex(string h) => ColorTranslator.FromHtml(h);

        public static Color Blend(Color a, Color b, double t)
            => Color.FromArgb((int)(a.R * t + b.R * (1 - t)), (int)(a.G * t + b.G * (1 - t)), (int)(a.B * t + b.B * (1 - t)));
    }

    static class Ui
    {

        static readonly string family = File.Exists(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Fonts), "segoeui.ttf"))
            ? Pick("Segoe UI", "Arial", "Tahoma")
            : Pick("Segoe UI Semilight", "Tahoma", "Arial");
        static readonly string semibold = Has("Segoe UI Semibold") ? "Segoe UI Semibold" : null;
        static readonly Dictionary<string, Font> cache = new Dictionary<string, Font>();

        static bool Has(string name)
        {
            try { return FontFamily.Families.Any(f => string.Equals(f.Name, name, StringComparison.OrdinalIgnoreCase)); }
            catch { return false; }
        }

        static string Pick(params string[] names)
        {
            foreach (var n in names) if (Has(n)) return n;
            return FontFamily.GenericSansSerif.Name;
        }

        public static Font F(float size, bool bold = false)
        {
            string key = size.ToString(CultureInfo.InvariantCulture) + (bold ? "b" : "");
            if (!cache.TryGetValue(key, out var f))
            {
                f = bold && semibold != null ? new Font(semibold, size) : new Font(family, size, bold ? FontStyle.Bold : FontStyle.Regular);
                cache[key] = f;
            }
            return f;
        }

        public static void HQ(Graphics g)
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.PixelOffsetMode = PixelOffsetMode.HighQuality;
        }

        public static GraphicsPath Round(RectangleF r, float rad)
        {
            var p = new GraphicsPath();
            float d = Math.Min(rad * 2, Math.Min(r.Width, r.Height));
            if (d <= 1) { p.AddRectangle(r); return p; }
            p.AddArc(r.X, r.Y, d, d, 180, 90);
            p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            p.CloseFigure();
            return p;
        }

        public static void FillRound(Graphics g, RectangleF r, float rad, Color c)
        {
            using (var p = Round(r, rad)) using (var b = new SolidBrush(c)) g.FillPath(b, p);
        }

        public static void DrawRound(Graphics g, RectangleF r, float rad, Color c, float w = 1f)
        {
            var rr = new RectangleF(r.X + w / 2f, r.Y + w / 2f, r.Width - w, r.Height - w);
            using (var p = Round(rr, rad)) using (var pen = new Pen(c, w)) g.DrawPath(pen, p);
        }

        public static LinearGradientBrush Grad(RectangleF r, Color a, Color b, float angle = 0f)
        {
            var rr = new RectangleF(r.X - 1, r.Y - 1, Math.Max(2, r.Width + 2), Math.Max(2, r.Height + 2));
            return new LinearGradientBrush(rr, a, b, angle);
        }

        public static void FillRoundGrad(Graphics g, RectangleF r, float rad, Color a, Color b, float angle = 0f)
        {
            using (var p = Round(r, rad)) using (var br = Grad(r, a, b, angle)) g.FillPath(br, p);
        }

        public static void DrawRoundGrad(Graphics g, RectangleF r, float rad, Color a, Color b, float w = 1f, float angle = 0f)
        {
            var rr = new RectangleF(r.X + w / 2f, r.Y + w / 2f, r.Width - w, r.Height - w);
            using (var p = Round(rr, rad)) using (var br = Grad(r, a, b, angle)) using (var pen = new Pen(br, w)) g.DrawPath(pen, p);
        }

        public static Color Lighter(Color c, double t = 0.12) => Theme.Blend(Color.White, c, t);
        public static Color Darker(Color c, double t = 0.12) => Theme.Blend(Color.Black, c, t);

        public static void GradText(Graphics g, string s, Font f, RectangleF r, Color a, Color b)
        {
            if (string.IsNullOrEmpty(s)) return;
            var old = g.TextRenderingHint;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
            using (var fmt = (StringFormat)StringFormat.GenericTypographic.Clone())
            {
                var sz = g.MeasureString(s, f, (int)r.Width, fmt);
                using (var br = Grad(new RectangleF(r.X, r.Y, Math.Min(r.Width, sz.Width), sz.Height), a, b, 0f))
                    g.DrawString(s, f, br, r, fmt);
            }
            g.TextRenderingHint = old;
        }

        public const TextFormatFlags Base = TextFormatFlags.NoPrefix | TextFormatFlags.NoPadding;

        public static void Text(Graphics g, string s, Font f, Rectangle r, Color c, TextFormatFlags fl = 0)
        {
            if (string.IsNullOrEmpty(s)) return;
            TextRenderer.DrawText(g, s, f, r, c, Base | fl);
        }

        public static void Text(Graphics g, string s, Font f, Rectangle r, Color c, Color bg, TextFormatFlags fl = 0)
        {
            if (string.IsNullOrEmpty(s)) return;
            TextRenderer.DrawText(g, s, f, r, c, bg, Base | fl);
        }

        public static int TextHeight(string s, Font f, int width)
        {
            if (string.IsNullOrEmpty(s)) return 0;
            return TextRenderer.MeasureText(s, f, new Size(Math.Max(1, width), int.MaxValue), Base | TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl).Height;
        }

        public static int TextWidth(string s, Font f) => string.IsNullOrEmpty(s) ? 0 : TextRenderer.MeasureText(s, f, Size.Empty, Base).Width;

        public static void Check(Graphics g, Rectangle b, bool on, bool enabled = true)
        {
            var p = Theme.P;
            if (on)
            {
                if (enabled) FillRoundGrad(g, b, 4, p.Accent, p.Accent2, 45f);
                else FillRound(g, b, 4, Theme.Blend(p.Accent, p.Card, 0.45));
                float s = b.Width / 20f;
                using (var pen = new Pen(Color.White, 2.2f * s) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
                    g.DrawLines(pen, new[] { new PointF(b.X + 5f * s, b.Y + 10.5f * s), new PointF(b.X + 8.6f * s, b.Y + 14f * s), new PointF(b.X + 15f * s, b.Y + 6.5f * s) });
            }
            else
            {
                FillRound(g, b, 4, p.Card);
                DrawRound(g, b, 4, enabled ? Theme.Blend(p.Muted, p.Card, 0.7) : p.Border, 1.5f);
            }
        }

        public static void Chevron(Graphics g, RectangleF r, bool left, Color c)
        {
            float cx = r.X + r.Width / 2, cy = r.Y + r.Height / 2, s = Math.Min(r.Width, r.Height) / 5f;
            using (var pen = new Pen(c, 2f) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
            {
                if (left) g.DrawLines(pen, new[] { new PointF(cx + s * 0.5f, cy - s), new PointF(cx - s * 0.5f, cy), new PointF(cx + s * 0.5f, cy + s) });
                else g.DrawLines(pen, new[] { new PointF(cx - s * 0.5f, cy - s), new PointF(cx + s * 0.5f, cy), new PointF(cx - s * 0.5f, cy + s) });
            }
        }

        public static void Triangle(Graphics g, Rectangle r, Color c)
        {
            float cx = r.X + r.Width / 2f, cy = r.Y + r.Height / 2f;
            using (var b = new SolidBrush(c))
                g.FillPolygon(b, new[] { new PointF(cx - 4.5f, cy - 2f), new PointF(cx + 4.5f, cy - 2f), new PointF(cx, cy + 3f) });
        }

        public static void Logo(Graphics g, Rectangle r, Image img, string brand)
        {
            if (img != null) { g.DrawImage(img, r); return; }
            FillRoundGrad(g, r, r.Width * 0.26f, Theme.P.Accent, Theme.P.Accent2, 45f);
            string letter = string.IsNullOrEmpty(brand) ? "W" : brand.Substring(0, 1).ToUpperInvariant();
            Text(g, letter, F(r.Height * 0.40f, true), r, Color.White, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
        }

        public static Icon AppIcon()
        {
            try { return Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch { return null; }
        }

        public static Image TryImage(string dir, params string[] names)
        {
            foreach (var n in names)
            {
                try
                {
                    string p = Path.Combine(dir, n);
                    if (File.Exists(p))
                        using (var s = new MemoryStream(File.ReadAllBytes(p))) return Image.FromStream(s);
                }
                catch { }
            }
            return null;
        }
    }

    static class Icons
    {
        static readonly Dictionary<string, Image> cache = new Dictionary<string, Image>(StringComparer.OrdinalIgnoreCase);

        public static Image Get(CatalogItem it)
        {
            if (it == null) return null;
            if (cache.TryGetValue(it.Id, out var img)) return img;
            var names = new List<string>();
            if (!string.IsNullOrEmpty(it.Icon)) names.Add(it.Icon);
            names.AddRange(new[] { it.Id + ".png", it.Id + ".ico", it.Id + ".jpg" });
            img = Ui.TryImage(Path.Combine(Paths.AssetsDir, "icons"), names.ToArray());
            cache[it.Id] = img;
            return img;
        }

        public static void Tile(Graphics g, Rectangle r, Image icon, string letter, Color color, bool dim = false)
        {
            var p = Theme.P;
            float rad = r.Width * 0.24f;
            if (icon != null)
            {
                Ui.FillRound(g, r, rad, Color.White);
                Ui.DrawRound(g, r, rad, Theme.Dark ? Color.FromArgb(70, 255, 255, 255) : p.Border);
                int pad = (int)(r.Width * 0.14f);
                var box = Rectangle.Inflate(r, -pad, -pad);

                float k = Math.Min((float)box.Width / icon.Width, (float)box.Height / icon.Height);
                int iw = Math.Max(1, (int)(icon.Width * k)), ih = Math.Max(1, (int)(icon.Height * k));
                var inner = new Rectangle(box.X + (box.Width - iw) / 2, box.Y + (box.Height - ih) / 2, iw, ih);
                var st = g.Save();
                using (var path = Ui.Round(r, rad)) g.SetClip(path);
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                using (var ia = new System.Drawing.Imaging.ImageAttributes())
                {
                    if (dim) ia.SetColorMatrix(new System.Drawing.Imaging.ColorMatrix { Matrix33 = 0.35f });
                    g.DrawImage(icon, inner, 0, 0, icon.Width, icon.Height, GraphicsUnit.Pixel, ia);
                }
                g.Restore(st);
                return;
            }
            Color c1 = Ui.Lighter(color, 0.15), c2 = Ui.Darker(color, 0.35);
            if (dim) { c1 = Theme.Blend(c1, p.Surface, 0.4); c2 = Theme.Blend(c2, p.Surface, 0.4); }
            Ui.FillRoundGrad(g, r, rad, c1, c2, 45f);
            Ui.Text(g, letter ?? "", Ui.F(r.Height * 0.30f, true), r, Color.White, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
        }
    }

    class BaseControl : Control
    {
        protected bool Hover, Down;

        public BaseControl()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
        }

        public Color? Fill;

        protected Color Bg => Fill ?? (Parent != null ? Parent.BackColor : Theme.P.Card);

        protected override void OnPaintBackground(PaintEventArgs e) => e.Graphics.Clear(Bg);
        protected override void OnMouseEnter(EventArgs e) { Hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { Hover = false; Down = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnMouseDown(MouseEventArgs e) { if (e.Button == MouseButtons.Left) { Down = true; Invalidate(); } base.OnMouseDown(e); }
        protected override void OnMouseUp(MouseEventArgs e) { Down = false; Invalidate(); base.OnMouseUp(e); }
        protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }
    }

    enum BtnKind { Primary, Secondary, Danger, DangerFill }

    class FlatButton : BaseControl
    {
        public BtnKind Kind;
        public bool DropArrow;
        bool toggled;
        public bool Toggled { get => toggled; set { toggled = value; Invalidate(); } }

        public FlatButton(string text, BtnKind kind = BtnKind.Secondary)
        {
            Text = text;
            Kind = kind;
            Cursor = Cursors.Hand;
            Font = Ui.F(10, true);
            Size = new Size(140, 42);
        }

        public int PreferredWidth => Ui.TextWidth(Text, Font) + (Toggled ? 62 : 40);

        protected override void OnTextChanged(EventArgs e) { Invalidate(); base.OnTextChanged(e); }

        protected override void OnClick(EventArgs e) { if (Enabled) base.OnClick(e); }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            var r = new Rectangle(0, 0, Width, Height);
            Color fill, border, fore;
            switch (Kind)
            {
                case BtnKind.Primary:
                    fill = Down ? Theme.Blend(p.AccentHover, Color.Black, 0.92) : Hover ? p.AccentHover : p.Accent;
                    border = fill; fore = p.OnAccent; break;
                case BtnKind.DangerFill:
                    fill = Hover ? Theme.Blend(p.Danger, Color.Black, 0.9) : p.Danger;
                    border = fill; fore = Color.White; break;
                case BtnKind.Danger:
                    fill = Hover ? p.DangerSoft : Theme.Blend(p.DangerSoft, Bg, 0.5);
                    border = Theme.Blend(p.Danger, Bg, 0.6); fore = p.Danger; break;
                default:
                    fill = toggled ? p.AccentSoft : Down ? p.Border : Hover ? p.SurfaceHover : p.Surface;
                    border = toggled ? p.Accent : p.Border; fore = p.Text; break;
            }
            if (!Enabled)
            {
                fill = Theme.Blend(fill, Bg, 0.45);
                border = Theme.Blend(border, Bg, 0.45);
                fore = Theme.Blend(fore, Bg, 0.4);
            }
            if (Kind == BtnKind.Primary && Enabled)
            {
                Color c1 = p.Accent, c2 = p.Accent2;
                if (Down) { c1 = Ui.Darker(c1); c2 = Ui.Darker(c2); }
                else if (Hover) { c1 = Ui.Lighter(c1); c2 = Ui.Lighter(c2); }
                Ui.FillRoundGrad(g, r, 6, c1, c2, 0f);
                Ui.Text(g, Text, Font, r, fore, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
                return;
            }
            Ui.FillRound(g, r, 6, fill);
            Ui.DrawRound(g, r, 6, border);
            var tr = r;
            if (toggled)
            {
                int tw = Ui.TextWidth(Text, Font);
                int x = (Width - tw - 24) / 2;
                Ui.Check(g, new Rectangle(x, (Height - 16) / 2, 16, 16), true, Enabled);
                tr = new Rectangle(x + 24, 0, tw + 4, Height);
                Ui.Text(g, Text, Font, tr, fore, fill, TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
                return;
            }
            if (DropArrow)
            {
                Ui.Triangle(g, new Rectangle(Width - 34, 0, 20, Height), p.Muted);
                tr = new Rectangle(16, 0, Width - 50, Height);
                Ui.Text(g, Text, Font, tr, fore, fill, TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
                return;
            }
            Ui.Text(g, Text, Font, tr, fore, fill, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
        }
    }

    class SelectCard : BaseControl
    {
        public string Overline, Title, Description;
        bool sel;
        public bool Selected { get => sel; set { sel = value; Invalidate(); } }

        public SelectCard() { Cursor = Cursors.Hand; }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            var r = new Rectangle(0, 0, Width, Height);
            Color fill = sel ? (Theme.Dark ? Theme.Blend(p.Accent, p.Card, 0.62) : p.AccentSoft) : Hover ? p.SurfaceHover : p.Surface;
            if (sel)
            {
                Color f2 = Theme.Dark ? Theme.Blend(p.Accent2, p.Card, 0.55) : p.AccentSoft2;
                Ui.FillRoundGrad(g, r, 10, fill, f2, 20f);
                Ui.DrawRoundGrad(g, r, 10, p.Accent, p.Accent2, 2f, 20f);
            }
            else
            {
                Ui.FillRound(g, r, 10, fill);
                Ui.DrawRound(g, r, 10, Hover ? p.AccentLine : p.Border, 1f);
            }
            Color text = sel && Theme.Dark ? Color.White : p.Text;
            Color muted = sel && Theme.Dark ? Color.FromArgb(225, 240, 238) : p.Muted;
            int pad = 22, w = Width - pad * 2 - 30;
            Ui.Text(g, Overline, Ui.F(8, true), new Rectangle(pad, pad, w, 18), sel ? (Theme.Dark ? Color.White : p.Accent) : p.Muted);
            Ui.Text(g, Title, Ui.F(19, true), new Rectangle(pad, pad + 20, w, 40), text, TextFormatFlags.SingleLine);
            Ui.Text(g, Description, Ui.F(9.5f), new Rectangle(pad, pad + 66, Width - pad * 2, Height - pad - 66 - 10), muted, TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis);

            var rb = new Rectangle(Width - pad - 20, pad, 20, 20);
            using (var pen = new Pen(sel ? (Theme.Dark ? Color.White : p.Accent) : Theme.Blend(p.Muted, fill, 0.6), 2f)) g.DrawEllipse(pen, rb);
            if (sel)
                using (var b = Theme.Dark ? (Brush)new SolidBrush(Color.White) : Ui.Grad(rb, p.Accent, p.Accent2, 45f)) g.FillEllipse(b, Rectangle.Inflate(rb, -5, -5));
        }
    }

    class CheckCard : BaseControl
    {
        public string Title, Description, Badge, Note;

        public string Highlight;
        public Color BadgeColor = Color.SlateGray;
        public Image Icon;
        public bool Risky, ShowInfo, Unavailable, ShowBadge;
        public object Data;
        bool chk, infoHover;

        public event EventHandler CheckedChanged, InfoClicked;

        public bool Checked
        {
            get => chk;
            set { if (chk == value) return; chk = value; Invalidate(); CheckedChanged?.Invoke(this, EventArgs.Empty); }
        }

        public CheckCard() { Cursor = Cursors.Hand; }

        Rectangle InfoRect => new Rectangle(Width - 44, (Height - 32) / 2, 32, 32);

        protected override void OnMouseMove(MouseEventArgs e)
        {
            bool h = ShowInfo && InfoRect.Contains(e.Location);
            if (h != infoHover) { infoHover = h; Invalidate(); }
            base.OnMouseMove(e);
        }

        protected override void OnMouseLeave(EventArgs e) { infoHover = false; base.OnMouseLeave(e); }

        protected override void OnMouseClick(MouseEventArgs e)
        {
            base.OnMouseClick(e);
            if (e.Button != MouseButtons.Left) return;
            if (ShowInfo && InfoRect.Contains(e.Location)) { InfoClicked?.Invoke(this, e); return; }
            if (!Enabled || Unavailable) return;
            Checked = !Checked;
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            var r = new Rectangle(0, 0, Width, Height);
            bool on = chk && !Unavailable;
            Color fill = on ? p.AccentSoft : Hover && !Unavailable ? p.SurfaceHover : p.Surface;
            if (on)
            {
                Ui.FillRoundGrad(g, r, 8, p.AccentSoft, p.AccentSoft2, 0f);
                Ui.DrawRoundGrad(g, r, 8, p.Accent, p.Accent2, 1.5f, 0f);
            }
            else
            {
                Ui.FillRound(g, r, 8, fill);
                Ui.DrawRound(g, r, 8, p.Border, 1f);
            }

            Color text = Unavailable ? Theme.Blend(p.Text, fill, 0.45) : p.Text;
            Color muted = Unavailable ? Theme.Blend(p.Muted, fill, 0.6) : p.Muted;
            int x = 16;
            Ui.Check(g, new Rectangle(x, (Height - 20) / 2, 20, 20), on, !Unavailable);
            x += 20 + 14;
            if (ShowBadge)
            {
                var br = new Rectangle(x, (Height - 40) / 2, 40, 40);
                Icons.Tile(g, br, Icon, Badge, BadgeColor, Unavailable);
                x += 40 + 14;
            }
            int right = Width - 14 - (ShowInfo ? 40 : 0) - (Risky ? 22 : 0);
            int tw = Math.Max(10, right - x);
            bool hl = !Unavailable && !string.IsNullOrEmpty(Highlight);
            string desc = Unavailable && !string.IsNullOrEmpty(Note) ? Note : hl ? Highlight : Description;
            var tf = Ui.F(10.5f, true);
            int th = TextRenderer.MeasureText("Ag", tf).Height;
            if (string.IsNullOrEmpty(desc))
                Ui.Text(g, Title, tf, new Rectangle(x, 0, tw, Height), text, TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
            else
            {
                var df = Ui.F(9f);
                int lines = Height >= 76 ? 2 : 1;
                int dh = TextRenderer.MeasureText("Ag", df).Height * lines;
                int top = (Height - th - 2 - dh) / 2;
                Ui.Text(g, Title, tf, new Rectangle(x, top, tw, th), text, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
                Ui.Text(g, desc, df, new Rectangle(x, top + th + 2, tw, dh), Unavailable ? Theme.Blend(p.Danger, fill, 0.8) : hl ? p.Accent : muted,
                    (lines > 1 ? TextFormatFlags.WordBreak : TextFormatFlags.SingleLine) | TextFormatFlags.EndEllipsis);
            }
            if (Risky)
                Ui.Text(g, "!", Ui.F(13, true), new Rectangle(right + 4, 0, 16, Height), p.Danger, TextFormatFlags.VerticalCenter | TextFormatFlags.HorizontalCenter | TextFormatFlags.SingleLine);
            if (ShowInfo)
            {
                var ir = InfoRect;
                if (infoHover) Ui.FillRound(g, ir, 6, p.Border);
                using (var b = new SolidBrush(p.Muted))
                    for (int i = -1; i <= 1; i++) g.FillEllipse(b, ir.X + ir.Width / 2f - 2, ir.Y + ir.Height / 2f - 2 + i * 6, 4, 4);
            }
        }
    }

    class ListRow : BaseControl
    {
        public string Title, Sub, RightText, RightSub;
        public bool RightAccent, Unavailable;
        bool sel;
        public bool Selected { get => sel; set { sel = value; Invalidate(); } }

        public ListRow() { Cursor = Cursors.Hand; }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            var r = new Rectangle(0, 0, Width, Height);
            Color fill = sel ? p.AccentSoft : Hover ? p.SurfaceHover : p.Surface;
            if (sel) { Ui.FillRoundGrad(g, r, 8, p.AccentSoft, p.AccentSoft2, 0f); Ui.DrawRoundGrad(g, r, 8, p.Accent, p.Accent2, 1.5f, 0f); }
            else { Ui.FillRound(g, r, 8, fill); Ui.DrawRound(g, r, 8, p.Border, 1f); }
            Color text = Unavailable ? Theme.Blend(p.Text, fill, 0.5) : p.Text;
            int rw = Math.Max(Ui.TextWidth(RightText, Ui.F(10, true)), Ui.TextWidth(RightSub, Ui.F(8.5f))) + 4;
            int pad = 14, tw = Width - pad * 2 - (rw > 4 ? rw + 12 : 0);
            var tf = Ui.F(10, true);
            int th = TextRenderer.MeasureText("Ag", tf).Height;
            int sh = string.IsNullOrEmpty(Sub) ? 0 : TextRenderer.MeasureText("Ag", Ui.F(8.5f)).Height;
            int top = (Height - th - sh - (sh > 0 ? 2 : 0)) / 2;
            Ui.Text(g, Title, tf, new Rectangle(pad, top, tw, th), text, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
            Ui.Text(g, Sub, Ui.F(8.5f), new Rectangle(pad, top + th + 2, tw, sh), p.Muted, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
            if (rw > 4)
            {
                var rr = new Rectangle(Width - pad - rw, top, rw, th);
                Ui.Text(g, RightText, Ui.F(10, true), rr, text, TextFormatFlags.Right | TextFormatFlags.SingleLine);
                Ui.Text(g, RightSub, Ui.F(8.5f), new Rectangle(rr.X, top + th + 2, rw, sh), RightAccent ? p.Accent : p.Muted, TextFormatFlags.Right | TextFormatFlags.SingleLine);
            }
        }
    }

    class NavList : BaseControl
    {
        public List<string> Items = new List<string>();
        public List<int> Counts = new List<int>();
        int sel;
        int hoverIdx = -1;
        const int RowH = 42;

        public event EventHandler SelectedChanged;

        public int SelectedIndex
        {
            get => sel;
            set { if (sel == value) return; sel = value; Invalidate(); SelectedChanged?.Invoke(this, EventArgs.Empty); }
        }

        public NavList() { Cursor = Cursors.Hand; }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            int i = e.Y / RowH;
            if (i >= Items.Count) i = -1;
            if (i != hoverIdx) { hoverIdx = i; Invalidate(); }
            base.OnMouseMove(e);
        }

        protected override void OnMouseLeave(EventArgs e) { hoverIdx = -1; base.OnMouseLeave(e); }

        protected override void OnMouseClick(MouseEventArgs e)
        {
            int i = e.Y / RowH;
            if (i >= 0 && i < Items.Count) SelectedIndex = i;
            base.OnMouseClick(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            for (int i = 0; i < Items.Count; i++)
            {
                var r = new Rectangle(0, i * RowH + 2, Width, RowH - 4);
                Color fill = Bg;
                if (i == sel)
                {
                    fill = p.AccentSoft;
                    Ui.FillRoundGrad(g, r, 6, p.AccentSoft, Bg, 0f);
                    Ui.FillRoundGrad(g, new Rectangle(0, r.Y + 6, 3, r.Height - 12), 1.5f, p.Accent, p.Accent2, 90f);
                }
                else if (i == hoverIdx) { fill = p.SurfaceHover; Ui.FillRound(g, r, 6, fill); }
                int cnt = i < Counts.Count ? Counts[i] : 0;
                int bw = 0;
                if (cnt > 0)
                {
                    string s = cnt.ToString(CultureInfo.InvariantCulture);
                    bw = Math.Max(22, Ui.TextWidth(s, Ui.F(8.5f, true)) + 12);
                    var br = new Rectangle(Width - bw - 10, r.Y + (r.Height - 20) / 2, bw, 20);
                    Ui.FillRoundGrad(g, br, 10, p.Accent, p.Accent2, 0f);
                    Ui.Text(g, s, Ui.F(8.5f, true), br, Color.White, TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
                }
                Ui.Text(g, Items[i], Ui.F(10, i == sel), new Rectangle(16, r.Y, Width - 32 - bw, r.Height), i == sel ? p.Text : p.Muted,
                    TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
            }
        }
    }

    class ThinProgress : BaseControl
    {
        double val;
        bool marquee;
        float phase;
        readonly Timer timer = new Timer { Interval = 30 };

        public ThinProgress()
        {
            Height = 8;
            timer.Tick += (s, e) => { phase = (phase + 0.012f) % 1.4f; Invalidate(); };
        }

        public double Value { get => val; set { val = Math.Max(0, Math.Min(100, value)); Invalidate(); } }

        public bool Marquee
        {
            get => marquee;
            set { marquee = value; timer.Enabled = value; Invalidate(); }
        }

        protected override void Dispose(bool disposing) { if (disposing) timer.Dispose(); base.Dispose(disposing); }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            var r = new RectangleF(0, 0, Width, Height);
            float rad = Height / 2f;
            Ui.FillRound(g, r, rad, Theme.Dark ? p.Border : Theme.Blend(p.Border, Color.White, 0.8));
            if (marquee)
            {
                float w = Width * 0.3f, x = (phase - 0.3f) * Width;
                var clip = g.Clip;
                using (var path = Ui.Round(r, rad)) g.SetClip(path);
                using (var br = Ui.Grad(new RectangleF(x, 0, w, Height), p.Accent, p.Accent2, 0f))
                using (var path = Ui.Round(new RectangleF(x, 0, w, Height), rad)) g.FillPath(br, path);
                g.Clip = clip;
            }
            else if (val > 0)
            {
                float w = (float)(Width * val / 100.0);
                using (var br = Ui.Grad(new RectangleF(0, 0, Width, Height), p.Accent, p.Accent2, 0f))
                using (var path = Ui.Round(new RectangleF(0, 0, Math.Max(Height, w), Height), rad)) g.FillPath(br, path);
            }
        }
    }

    class Pager : BaseControl
    {
        int page, count = 1;
        public event EventHandler PageChanged;

        public int Page
        {
            get => page;
            set
            {
                int v = Math.Max(0, Math.Min(count - 1, value));
                if (v == page) return;
                page = v; Invalidate(); PageChanged?.Invoke(this, EventArgs.Empty);
            }
        }

        public int Count
        {
            get => count;
            set { count = Math.Max(1, value); if (page >= count) page = count - 1; Invalidate(); }
        }

        public Pager() { Size = new Size(120, 32); Cursor = Cursors.Hand; }

        protected override void OnMouseClick(MouseEventArgs e)
        {
            if (e.X < 34) Page--;
            else if (e.X > Width - 34) Page++;
            base.OnMouseClick(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            var l = new Rectangle(0, 0, 32, Height);
            var rr = new Rectangle(Width - 32, 0, 32, Height);
            foreach (var (rect, left, en) in new[] { (l, true, page > 0), (rr, false, page < count - 1) })
            {
                Ui.FillRound(g, rect, 6, p.Surface);
                Ui.DrawRound(g, rect, 6, p.Border);
                Ui.Chevron(g, rect, left, en ? p.Text : Theme.Blend(p.Muted, p.Card, 0.4));
            }
            Ui.Text(g, (page + 1) + " / " + count, Ui.F(9.5f, true), new Rectangle(32, 0, Width - 64, Height), p.Muted,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
        }
    }

    class TextField : BaseControl
    {
        public readonly TextBox Box = new TextBox { BorderStyle = BorderStyle.None };

        public TextField()
        {
            Height = 40;
            Box.Font = Ui.F(10.5f);
            Controls.Add(Box);
            Box.GotFocus += (s, e) => Invalidate();
            Box.LostFocus += (s, e) => Invalidate();
            Cursor = Cursors.IBeam;
            Click += (s, e) => Box.Focus();
        }

        public override string Text { get => Box.Text; set => Box.Text = value; }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            Box.SetBounds(12, (Height - Box.PreferredHeight) / 2, Width - 24, Box.PreferredHeight);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            Box.BackColor = p.Surface;
            Box.ForeColor = p.Text;
            Ui.FillRound(g, new Rectangle(0, 0, Width, Height), 6, p.Surface);
            Ui.DrawRound(g, new Rectangle(0, 0, Width, Height), 6, Box.Focused ? p.Accent : p.Border, Box.Focused ? 1.5f : 1f);
        }
    }

    class Dialog : Form
    {
        readonly string title, body;
        int result = -1;
        readonly List<Control> rows = new List<Control>();
        const int W = 560, Pad = 28;
        int bodyH;

        Dialog(string title, string body)
        {
            this.title = title;
            this.body = body;
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            KeyPreview = true;
            BackColor = Theme.P.Card;
            DoubleBuffered = true;
            Font = Ui.F(10);
            bodyH = Ui.TextHeight(body, Ui.F(10), W - Pad * 2);
        }

        void Finish(int r) { result = r; Close(); }

        protected override void OnKeyDown(KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Escape) Finish(-1);
            base.OnKeyDown(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            var g = e.Graphics;
            Ui.HQ(g);
            var p = Theme.P;
            using (var pen = new Pen(p.Border)) g.DrawRectangle(pen, 0, 0, Width - 1, Height - 1);
            using (var b = Ui.Grad(new RectangleF(0, 0, Width, 4), p.Accent, p.Accent2, 0f)) g.FillRectangle(b, 0, 0, Width, 4);
            Ui.Text(g, title, Ui.F(14, true), new Rectangle(Pad, Pad - 2, W - Pad * 2, 32), p.Text, TextFormatFlags.SingleLine | TextFormatFlags.EndEllipsis);
            Ui.Text(g, body, Ui.F(10), new Rectangle(Pad, Pad + 40, W - Pad * 2, bodyH + 4), p.Text, TextFormatFlags.WordBreak);
        }

        static void Place(Dialog d, IWin32Window owner)
        {
            var ow = owner as Form;
            if (ow != null && ow.Visible)
            {
                d.StartPosition = FormStartPosition.Manual;
                var b = ow.Bounds;
                d.Location = new Point(b.X + (b.Width - d.Width) / 2, b.Y + (b.Height - d.Height) / 2);
            }
            else d.StartPosition = FormStartPosition.CenterScreen;
        }

        public static int Ask(IWin32Window owner, string title, string body, BtnKind firstKind, params string[] buttons)
        {
            using (var d = new Dialog(title, body))
            {
                int y = Pad + 40 + d.bodyH + 24;
                int x = W - Pad;
                for (int i = 0; i < buttons.Length; i++)
                {
                    var b = new FlatButton(buttons[i], i == 0 ? firstKind : BtnKind.Secondary);
                    b.Width = Math.Max(110, b.PreferredWidth);
                    x -= b.Width;
                    b.Location = new Point(x, y);
                    x -= 10;
                    int idx = i;
                    b.Click += (s, e) => d.Finish(idx);
                    d.Controls.Add(b);
                }
                d.ClientSize = new Size(W, y + 42 + Pad);
                Place(d, owner);
                d.ShowDialog(owner);
                return d.result;
            }
        }

        public static void Info(IWin32Window owner, string title, string body) => Ask(owner, title, body, BtnKind.Primary, "Понятно");

        public static int Pick(IWin32Window owner, string title, string body, IList<(string title, string sub)> items, int selected)
        {
            using (var d = new Dialog(title, body))
            {
                int y = Pad + 40 + d.bodyH + 16;
                const int rowH = 50;
                int max = Math.Min(items.Count, 9);
                var panel = new Panel { Location = new Point(Pad, y), Size = new Size(W - Pad * 2, max * (rowH + 6)), AutoScroll = items.Count > max, BackColor = Theme.P.Card };
                for (int i = 0; i < items.Count; i++)
                {
                    var row = new ListRow { Title = items[i].title, Sub = items[i].sub, Selected = i == selected, Bounds = new Rectangle(0, i * (rowH + 6), panel.Width - (panel.AutoScroll ? 20 : 0), rowH) };
                    int idx = i;
                    row.Click += (s, e) => d.Finish(idx);
                    panel.Controls.Add(row);
                }
                d.Controls.Add(panel);
                y = panel.Bottom + 18;
                var cancel = new FlatButton("Отмена") { Width = 120, Location = new Point(W - Pad - 120, y) };
                cancel.Click += (s, e) => d.Finish(-1);
                d.Controls.Add(cancel);
                d.ClientSize = new Size(W, y + 42 + Pad);
                Place(d, owner);
                d.ShowDialog(owner);
                return d.result;
            }
        }
    }
}
