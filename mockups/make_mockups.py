# Gera mockups do mostrador Flamengo para Pebble Time 2 (emery, 200x228, 64 cores)
from PIL import Image, ImageDraw, ImageFont
import math, os
OUT = os.path.dirname(os.path.abspath(__file__))
W, H, S = 200, 228, 4
G = "/usr/share/fonts/truetype/sand-box/google/"
def font(path, size, var=None):
    f = ImageFont.truetype(G + path, int(size * S))
    if var:
        try: f.set_variation_by_name(var)
        except Exception: pass
    return f
ANTON = lambda s: font("Anton/Anton-Regular.ttf", s)
BEBAS = lambda s: font("Bebas Neue/BebasNeue-Regular.ttf", s)
BARB = lambda s: font("Barlow Condensed/BarlowCondensed-Bold.ttf", s)
BARK = lambda s: font("Barlow Condensed/BarlowCondensed-Black.ttf", s)
BARM = lambda s: font("Barlow Condensed/BarlowCondensed-Medium.ttf", s)
ORB = lambda s: font("Orbitron/Orbitron-VariableFont_wght.ttf", s, "Bold")

# Pebble 64-colour palette: each channel in {00,55,AA,FF}
RED, DRED, MAROON = (255,0,0), (170,0,0), (85,0,0)
BLK, WHT = (0,0,0), (255,255,255)
LGRY, DGRY = (170,170,170), (85,85,85)
YEL, CHROME, ORANGE = (255,255,85), (255,170,0), (255,85,0)
GRN, PICTON, PASTELY = (0,170,0), (85,170,255), (255,255,170)
LEDOFF = (85,0,0)

class C:
    def __init__(self, bg=BLK):
        self.im = Image.new("RGB", (W*S, H*S), bg)
        self.d = ImageDraw.Draw(self.im)
    def s(self, v): return [round(x*S) for x in v]
    def rect(self, b, fill=None, outline=None, w=1, r=0):
        if r: self.d.rounded_rectangle(self.s(b), r*S, fill=fill, outline=outline, width=round(w*S) if outline else 0)
        else: self.d.rectangle(self.s(b), fill=fill, outline=outline, width=round(w*S) if outline else 0)
    def ell(self, b, fill=None, outline=None, w=1):
        self.d.ellipse(self.s(b), fill=fill, outline=outline, width=round(w*S) if outline else 0)
    def poly(self, pts, fill=None):
        self.d.polygon([(x*S, y*S) for x, y in pts], fill=fill)
    def line(self, pts, fill, w=1):
        self.d.line([(x*S, y*S) for x, y in pts], fill=fill, width=round(w*S))
    def arc(self, b, a0, a1, fill, w=1):
        self.d.arc(self.s(b), a0, a1, fill=fill, width=round(w*S))
    def text(self, xy, t, f, fill, anchor="la"):
        self.d.text((xy[0]*S, xy[1]*S), t, font=f, fill=fill, anchor=anchor)
    def tw(self, t, f):
        b = self.d.textbbox((0,0), t, font=f); return (b[2]-b[0])/S
    def finish(self, name):
        im = self.im.resize((W, H), Image.LANCZOS)
        # snap every pixel to the Pebble 64-colour palette
        lut = [min((0,85,170,255), key=lambda c: abs(c-v)) for v in range(256)]
        im = im.point(lut*3)
        im.save(f"{OUT}/{name}.png")
        im.resize((W*3, H*3), Image.NEAREST).save(f"{OUT}/{name}@3x.png")
        return im

# ---------- shared original graphics ----------
def shield(c, x, y, w, h, stripes=6, outline=WHT):
    """Generic shield (not the club crest) with rubro-negro horizontal stripes and CRF-less design."""
    pts = [(x, y), (x+w, y), (x+w, y+h*0.55), (x+w/2, y+h), (x, y+h*0.55)]
    m = Image.new("L", c.im.size, 0); ImageDraw.Draw(m).polygon([(px*S, py*S) for px, py in pts], fill=255)
    st = Image.new("RGB", c.im.size, BLK); sd = ImageDraw.Draw(st)
    for i in range(stripes):
        y0 = y + h*i/stripes; y1 = y + h*(i+1)/stripes
        sd.rectangle([x*S, y0*S, (x+w)*S, y1*S], fill=RED if i % 2 == 0 else BLK)
    c.im.paste(st, (0,0), m)
    c.d.line([(px*S, py*S) for px, py in pts + [pts[0]]], fill=outline, width=round(1.5*S), joint="curve")

def urubu(c, cx, cy, s, col):
    """Abstract vulture-in-flight: two swept wings + small head (original glyph)."""
    c.poly([(cx, cy), (cx-1.0*s, cy-0.45*s), (cx-0.75*s, cy-0.15*s), (cx-1.1*s, cy-0.05*s), (cx-0.3*s, cy+0.2*s)], col)
    c.poly([(cx, cy), (cx+1.0*s, cy-0.45*s), (cx+0.75*s, cy-0.15*s), (cx+1.1*s, cy-0.05*s), (cx+0.3*s, cy+0.2*s)], col)
    c.ell([cx-0.13*s, cy-0.3*s, cx+0.13*s, cy-0.04*s], fill=col)

def sun_cloud(c, x, y, s):
    cx, cy, r = x+s*0.38, y+s*0.36, s*0.17
    for k in range(8):
        a = k*math.pi/4
        c.line([(cx+math.cos(a)*r*1.45, cy+math.sin(a)*r*1.45), (cx+math.cos(a)*r*2.0, cy+math.sin(a)*r*2.0)], CHROME, max(1, s/14))
    c.ell([cx-r, cy-r, cx+r, cy+r], fill=CHROME)
    c.ell([x+s*0.35, y+s*0.45, x+s*0.7, y+s*0.8], fill=LGRY)
    c.ell([x+s*0.55, y+s*0.52, x+s*0.95, y+s*0.85], fill=LGRY)
    c.rect([x+s*0.42, y+s*0.66, x+s*0.85, y+s*0.85], fill=LGRY)

def battery(c, x, y, w, h, pct, col=WHT, fillc=RED):
    c.rect([x, y, x+w, y+h], outline=col, w=1.2)
    c.rect([x+w, y+h*0.3, x+w+2, y+h*0.7], fill=col)
    c.rect([x+2, y+2, x+2+(w-4)*pct/100, y+h-2], fill=fillc)

# ======================= 1. CLÁSSICO LISTRADO =======================
def m1():
    c = C()
    n = 10
    for i in range(n):
        c.rect([0, H*i/n, W, H*(i+1)/n], fill=RED if i % 2 == 0 else BLK)
    # top: shield + MENGÃO
    c.rect([0, 0, W, 34], fill=BLK)
    shield(c, 10, 5, 19, 24, 5)
    c.text((36, 17), "MENGÃO", BARK(22), WHT, "lm")
    c.text((190, 17), "SÁB 24 OUT", BARB(15), RED, "rm")
    c.line([(0, 34), (W, 34)], WHT, 2)
    # big time on stripes with heavy black outline
    f = ANTON(80)
    c.d.text((W/2*S, 98*S), "14:37", font=f, fill=WHT, anchor="mm", stroke_width=round(4*S), stroke_fill=BLK)
    # game card
    c.rect([8, 146, 192, 182], fill=WHT, r=6)
    c.rect([8, 146, 52, 182], fill=BLK, r=6); c.rect([40, 146, 52, 182], fill=BLK)
    c.text((30, 164), "BRA", BARK(15), RED, "mm")
    c.text((60, 164), "FLA x PAL", BARK(20), BLK, "lm")
    c.text((186, 164), "DOM 16h", BARB(17), DRED, "rm")
    # bottom bar
    c.rect([0, 192, W, H], fill=BLK)
    c.line([(0, 192), (W, 192)], WHT, 2)
    sun_cloud(c, 8, 197, 26)
    c.text((38, 210), "27°", BARK(22), WHT, "lm")
    c.text((70, 203), "31°", BARB(11), RED, "lm"); c.text((70, 216), "22°", BARB(11), PICTON, "lm")
    battery(c, 140, 204, 22, 12, 80)
    c.text((191, 210), "80%", BARB(14), WHT, "rm")
    return c.finish("1-classico-listrado")

# ======================= 2. MINIMALISTA =======================
def m2():
    c = C(BLK)
    f = BEBAS(112)
    c.text((10, 8), "14", f, WHT, "la")
    c.text((10, 104), "37", f, RED, "la")
    # thin red vertical rule
    c.line([(120, 18), (120, 210)], RED, 1.5)
    x = 130
    c.text((x, 22), "SÁB", BARB(20), LGRY, "lm")
    c.text((x, 48), "24", BARK(34), WHT, "lm")
    c.text((x, 74), "OUT", BARB(20), LGRY, "lm")
    c.line([(x, 92), (190, 92)], DGRY, 1)
    sun_cloud(c, x-2, 98, 22)
    c.text((x+24, 110), "27°", BARB(22), WHT, "lm")
    c.line([(x, 130), (190, 130)], DGRY, 1)
    # game
    urubu(c, x+10, 146, 10, RED)
    c.text((x+24, 145), "PAL", BARK(18), WHT, "lm")
    c.text((x, 166), "DOM 16h", BARB(17), RED, "lm")
    c.line([(x, 184), (190, 184)], DGRY, 1)
    battery(c, x, 196, 20, 10, 80, LGRY, WHT)
    c.text((x+28, 201), "80%", BARB(14), LGRY, "lm")
    return c.finish("2-minimalista")

# ======================= 3. PLACAR / ESTÁDIO =======================
GLY = {  # 5x7 dot-matrix
 "0":["01110","10001","10011","10101","11001","10001","01110"], "1":["00100","01100","00100","00100","00100","00100","01110"],
 "2":["01110","10001","00001","00010","00100","01000","11111"], "3":["11110","00001","00001","01110","00001","00001","11110"],
 "4":["00010","00110","01010","10010","11111","00010","00010"], "5":["11111","10000","11110","00001","00001","10001","01110"],
 "6":["00110","01000","10000","11110","10001","10001","01110"], "7":["11111","00001","00010","00100","01000","01000","01000"],
 "8":["01110","10001","10001","01110","10001","10001","01110"], "9":["01110","10001","10001","01111","00001","00010","01100"],
 ":":["00000","00100","00100","00000","00100","00100","00000"], "'":["00100","00100","01000","00000","00000","00000","00000"],
}
def led(c, x, y, txt, p, on, off=None, gap=1, r=0.42):
    """Dot-matrix LED: square-ish lit pixels on an integer grid, optional faint unlit dots."""
    for ch in txt:
        g = GLY[ch]; cols = 5 if ch not in ":'" else 3
        for j, row in enumerate(g):
            sub = row if cols == 5 else row[1:4]
            for i, b in enumerate(sub):
                x0, y0 = x + i*p, y + j*p
                if b == "1":
                    c.rect([x0, y0, x0+p-1, y0+p-1], fill=on, r=max(0, (p-1)*0.25))
                elif off:
                    m = p/2
                    c.rect([x0+m-1, y0+m-1, x0+m, y0+m], fill=off)
        x += (cols + gap) * p
    return x
def led_w(txt, p, gap=1): return sum(((5 if ch not in ":'" else 3) + gap) * p for ch in txt) - gap*p

def m3():
    c = C(BLK)
    # header: clock in amber LEDs
    p = 5
    x0 = round((W - led_w("14:37", p)) / 2)
    led(c, x0, 6, "14:37", p, CHROME, (85,85,0))
    c.text((W/2, 50), "SÁB 24 OUT", BARB(14), LGRY, "mm")
    # scoreboard panel
    c.rect([4, 60, 196, 186], fill=(0,0,0), outline=DGRY, w=1.5, r=4)
    c.rect([4, 60, 196, 76], fill=DRED, r=4); c.rect([4, 70, 196, 76], fill=DRED)
    c.text((W/2, 68), "BRASILEIRÃO", BARK(13), WHT, "mm")
    # team tags
    c.rect([12, 82, 90, 102], fill=RED); c.rect([110, 82, 188, 102], fill=GRN)
    c.text((51, 92), "FLA", BARK(18), WHT, "mm"); c.text((149, 92), "PAL", BARK(18), WHT, "mm")
    p2 = 7
    led(c, round(51 - led_w("2", p2)/2), 106, "2", p2, RED, MAROON)
    led(c, round(149 - led_w("1", p2)/2), 106, "1", p2, RED, MAROON)
    c.rect([94, 128, 106, 132], fill=LGRY)
    # live minute
    c.ell([44, 168, 52, 176], fill=RED)
    c.text((56, 172), "AO VIVO", BARK(15), WHT, "lm")
    led(c, 116, 162, "67'", 3, YEL)
    # bottom row
    sun_cloud(c, 8, 192, 30)
    c.text((42, 208), "27°C", BARK(20), WHT, "lm")
    battery(c, 126, 202, 22, 12, 80, WHT, GRN)
    c.text((192, 208), "80%", BARB(16), WHT, "rm")
    return c.finish("3-placar-estadio")

# ======================= 4. ANALÓGICO =======================
def m4():
    c = C(BLK)
    cx, cy, R = 100, 106, 92
    # rubro-negro dial: concentric outer ring with alternating red/black segments
    for k in range(60):
        a0, a1 = k*6-90, (k+1)*6-90
        c.d.pieslice(c.s([cx-R, cy-R, cx+R, cy+R]), a0, a1, fill=RED if (k//5) % 2 == 0 else DRED)
    c.ell([cx-R+9, cy-R+9, cx+R-9, cy+R-9], fill=BLK)
    # inner faint stripes band
    for k in range(12):
        a = math.radians(k*30-90)
        L = 12 if k % 3 == 0 else 6
        r0 = R-12
        c.line([(cx+math.cos(a)*r0, cy+math.sin(a)*r0), (cx+math.cos(a)*(r0-L), cy+math.sin(a)*(r0-L))], WHT, 3 if k % 3 == 0 else 1.6)
    # complications
    c.text((cx, cy-48), "SÁB 24", BARK(17), WHT, "mm")
    sun_cloud(c, cx-62, cy-30, 24); c.text((cx-50, cy+2), "27°", BARB(17), WHT, "mm")
    bx, by = cx+48, cy-24
    c.arc([bx-14, by-14, bx+14, by+14], -90, -90+360*0.8, RED, 3)
    c.arc([bx-14, by-14, bx+14, by+14], -90+360*0.8, 270, DGRY, 3)
    c.text((bx, by), "80", BARB(13), WHT, "mm")
    c.rect([cx-40, cy+44, cx+40, cy+64], fill=RED, r=10)
    c.text((cx, cy+54), "PAL · DOM 16h", BARK(14), WHT, "mm")
    urubu(c, cx+30, cy+30, 11, RED)
    # hands 14:37
    hh, mm = 14 % 12, 37
    ah = math.radians((hh + mm/60)*30 - 90); am = math.radians(mm*6 - 90)
    def hand(a, L, wdt, col, tail=10):
        px, py = -math.sin(a), math.cos(a)
        tip = (cx+math.cos(a)*L, cy+math.sin(a)*L)
        back = (cx-math.cos(a)*tail, cy-math.sin(a)*tail)
        c.poly([(back[0]+px*wdt/2, back[1]+py*wdt/2), (tip[0]+px*wdt/2.6, tip[1]+py*wdt/2.6),
                (tip[0]-px*wdt/2.6, tip[1]-py*wdt/2.6), (back[0]-px*wdt/2, back[1]-py*wdt/2)], col)
    hand(ah, 44, 12, BLK); hand(ah, 42, 8, WHT)
    hand(am, 76, 9, BLK); hand(am, 74, 6, WHT)
    c.ell([cx-7, cy-7, cx+7, cy+7], fill=RED); c.ell([cx-2, cy-2, cx+2, cy+2], fill=BLK)
    # footer
    c.text((W/2, 216), "MENGÃO", BARK(18), RED, "mm")
    return c.finish("4-analogico")

ims = [m1(), m2(), m3(), m4()]
names = ["1 · Clássico Listrado", "2 · Minimalista", "3 · Placar / Estádio", "4 · Analógico"]
# contact sheet
sw, sh = W*3, H*3
pad, bz = 40, 46
cw = sw + 2*bz
sheet_w = 4*cw + 5*pad
sheet_h = sh + 2*bz + 160
sheet = Image.new("RGB", (sheet_w, sheet_h), (34,34,38))
d = ImageDraw.Draw(sheet)
tf = ImageFont.truetype(G+"Barlow Condensed/BarlowCondensed-Bold.ttf", 52)
hf = ImageFont.truetype(G+"Barlow Condensed/BarlowCondensed-Medium.ttf", 30)
for i, im in enumerate(ims):
    x = pad + i*(cw+pad); y = pad
    d.rounded_rectangle([x, y, x+cw, y+sh+2*bz], 60, fill=(18,18,18), outline=(110,110,115), width=6)
    d.rounded_rectangle([x+bz-8, y+bz-8, x+bz+sw+8, y+bz+sh+8], 14, fill=(0,0,0))
    sheet.paste(im.resize((sw, sh), Image.NEAREST), (x+bz, y+bz))
    d.text((x+cw/2, y+sh+2*bz+48), names[i], font=tf, fill=(255,255,255), anchor="mm")
d.text((sheet_w/2, sheet_h-30), "Mockups Mengão · Pebble Time 2 (200×228, 64 cores) · dados de exemplo", font=hf, fill=(170,170,170), anchor="mm")
sheet.save(f"{OUT}/contact-sheet.png")
print("ok")
