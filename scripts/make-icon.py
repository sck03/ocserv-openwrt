"""Render the project's original vector monogram to a multi-resolution Windows icon."""
from pathlib import Path
from PIL import Image, ImageDraw

ROOT=Path(__file__).resolve().parents[1]
SCALE=4
size=256*SCALE
base=Image.new('RGBA',(size,size),(0,0,0,0))
gradient=Image.new('RGBA',(size,size))
pixels=gradient.load()
for y in range(size):
    for x in range(size):
        t=(x+y)/(2*(size-1))
        pixels[x,y]=(round(29*(1-t)+5*t),round(55*(1-t)+127*t),round(113*(1-t)+155*t),255)
mask=Image.new('L',(size,size))
ImageDraw.Draw(mask).rounded_rectangle((8*SCALE,8*SCALE,248*SCALE,248*SCALE),radius=54*SCALE,fill=255)
base.paste(gradient,(0,0),mask)

def path(commands):
    result=[]
    current=(0,0)
    for command in commands:
        if command[0]=='M' or command[0]=='L':
            current=command[1:]
            result.append(tuple(v*SCALE for v in current))
        elif command[0]=='C':
            x0,y0=current
            x1,y1,x2,y2,x3,y3=command[1:]
            for i in range(1,33):
                t=i/32; s=1-t
                result.append(((s*s*s*x0+3*s*s*t*x1+3*s*t*t*x2+t*t*t*x3)*SCALE,
                               (s*s*s*y0+3*s*s*t*y1+3*s*t*t*y2+t*t*t*y3)*SCALE))
            current=(x3,y3)
    return result

shield=path([('M',128,33),('C',156,48,180,55,208,58),('L',208,119),('C',208,165,179,200,128,224),('C',77,200,48,165,48,119),('L',48,58),('C',76,55,100,48,128,33)])
overlay=Image.new('RGBA',base.size)
draw=ImageDraw.Draw(overlay)
draw.polygon(shield,fill=(255,255,255,18))
draw.line(shield+[shield[0]],fill=(113,228,234,140),width=5*SCALE,joint='curve')
base=Image.alpha_composite(base,overlay)
letter=Image.new('L',base.size)
draw=ImageDraw.Draw(letter)
draw.polygon(path([('M',87,64),('L',132,64),('C',162,64,178,77,178,98),('C',178,112,170,122,158,127),('C',174,132,183,144,183,160),('C',183,184,164,198,133,198),('L',87,198)]),fill=255)
draw.polygon(path([('M',111,86),('L',111,117),('L',130,117),('C',145,117,153,111,153,101),('C',153,91,145,86,130,86)]),fill=0)
draw.polygon(path([('M',111,138),('L',111,176),('L',131,176),('C',148,176,157,169,157,157),('C',157,145,148,138,131,138)]),fill=0)
base.paste((255,255,255,255),(0,0,size,size),letter)
base=base.resize((256,256),Image.Resampling.LANCZOS)
base.save(ROOT/'resources'/'app.png')
base.save(ROOT/'resources'/'app.ico',sizes=[(s,s) for s in (16,20,24,32,40,48,64,96,128,256)])
print('Generated app.ico at 16-256 px and app.png from the original SVG design.')
