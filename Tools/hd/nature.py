"""Procedural terrain, foliage and Otis coat recipes from the v5.2–v5.4 releases."""
import hashlib
import struct
from math import atan2, ceil, cos, floor, hypot, pi, sin
from pathlib import Path
from random import Random
from struct import pack_into, unpack_from

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFilter
import hd_codec as hd

groups = {
    'grass': 'grassmain grassbckg grasstest3 grasstest grass_1 golf_green large_tuft',
    'tuft': 'grass_tufts_01 grass_tufts_2',
    'mixed': 'grass2hay drtgtrack',
    'soil': 'drtbtrack drtbtile drtrdrk drtrtile drtrtire drtroad patchgrnd_new patchtile_new rock_ground',
    'fine': 'mudpuddleyard sand',
    'stubble': 'stubble_tile_01',
}

def wrapped(image, operation, wrap, pad, output_size=None):
    a = np.asarray(image)
    nx = ((0,0),(pad,pad)) + (((0,0),) if a.ndim == 3 else ())
    ny = ((pad,pad),(0,0)) + (((0,0),) if a.ndim == 3 else ())
    a = np.pad(a,nx,mode='wrap' if wrap[0] else 'edge')
    a = np.pad(a,ny,mode='wrap' if wrap[1] else 'edge')
    padded = Image.fromarray(a)
    if output_size is None:
        changed = operation(padded)
        return changed.crop((pad,pad,pad+image.width,pad+image.height))
    w,h = output_size
    px,py = pad*w//image.width,pad*h//image.height
    assert px*image.width == pad*w and py*image.height == pad*h
    changed = operation(padded,(w+2*px,h+2*py))
    return changed.crop((px,py,px+w,py+h))

def resample(image, size, wrap):
    if image.size == size:
        return image
    return wrapped(image,lambda im, sz: im.resize(sz,Image.Resampling.LANCZOS),wrap,4,size)

def blur(im,radius,wrap):
    return wrapped(im,lambda x:x.filter(ImageFilter.GaussianBlur(radius)),wrap,16)

def values(im):
    return np.asarray(im,dtype=np.float32)

def grain(size,wrap,seed):
    w,h=size
    noise = np.random.default_rng(seed).normal(128.0,9.0,(h,w))
    im = Image.fromarray(np.uint8(np.clip(noise,0,255)))
    return 0.60*(values(im)-values(blur(im,1.5,wrap))) + 0.52*(values(blur(im,1.1,wrap))-values(blur(im,5.0,wrap)))

def painted_relief(size,wrap,rng,kind):
    w,h=size
    margin=12
    canvas = Image.new('L',(w+2*margin,h+2*margin),128)
    draw=ImageDraw.Draw(canvas)
    fibers = kind in ('grass','tuft','stubble')
    for _ in range(w*h//(27 if fibers else 92)):
        x,y=rng.uniform(0,w),rng.uniform(0,h)
        if fibers:
            angle = rng.uniform(-pi,pi) if kind != 'tuft' else rng.uniform(-2.30,-0.84)
            length = rng.uniform(2.0,7.8) * (1.25 if kind=='stubble' else 1.0)
            dx,dy=cos(angle)*length,sin(angle)*length
            bend=rng.uniform(-1.0,1.0)
            shade,light=rng.randint(105,121),rng.randint(134,153)
            stroke = light if rng.random()>.23 else shade
        else:
            rx,ry=rng.uniform(.45,2.25),rng.uniform(.45,1.65)
            shade,light=rng.randint(109,123),rng.randint(132,147)
        xs=[x]
        ys=[y]
        if wrap[0] and x<margin: xs.append(x+w)
        if wrap[0] and x>w-margin: xs.append(x-w)
        if wrap[1] and y<margin: ys.append(y+h)
        if wrap[1] and y>h-margin: ys.append(y-h)
        for ax in xs:
            for ay in ys:
                p,q=ax+margin,ay+margin
                if fibers:
                    path=((p,q),(p+dx*.55,q+dy*.55),(p+dx+bend,q+dy))
                    draw.line([(a+.80,b+.62) for a,b in path],fill=shade)
                    draw.line(path,fill=stroke)
                else:
                    draw.ellipse((p-rx+.65,q-ry+.7,p+rx+.65,q+ry+.7),fill=shade)
                    draw.ellipse((p-rx,q-ry,p+rx*.62,q+ry*.50),fill=light)
    layer=canvas.crop((margin,margin,margin+w,margin+h))
    softened=blur(layer,.42,wrap)
    return values(softened)-values(blur(softened,5.0,wrap))

def get_original_alpha_mips(raw):
    info=hd.dds_info(raw)
    fmt=info['format']
    offset=128
    result=[]
    for w,h in hd.mip_sizes(info['width'],info['height'])[:info['mips']]:
        length=hd.level_bytes(fmt,w,h)
        frame=hd.dds_header(fmt,w,h,1)+raw[offset:offset+length]
        result.append(hd.decode_top(frame,hd.dds_info(frame)).convert('RGBA').getchannel('A'))
        offset+=length
    assert offset==len(raw)
    return result

def weighted_rgb(rgb,alpha):
    return np.sum(rgb*alpha[...,None],axis=(0,1))/alpha.sum()

def write_texture(rgb,alpha_chain):
    sizes=hd.mip_sizes(*rgb.size)
    header=bytearray(hd.dds_header('A8R8G8B8',*rgb.size,len(sizes)))
    flags,=struct.unpack_from('<I',header,8)
    struct.pack_into('<I',header,8,(flags & ~0x80000) | 0x8)
    struct.pack_into('<I',header,20,rgb.width*4)
    parts=[bytes(header)]
    for index,size in enumerate(sizes):
        if index:
            rgb=rgb.resize(size,Image.Resampling.BOX)
        alpha=alpha_chain[index]
        assert alpha.size==rgb.size
        rgba=rgb.convert('RGBA')
        rgba.putalpha(alpha)
        parts.append(rgba.tobytes('raw','BGRA'))
    output=b''.join(parts)
    decoded=hd.dds_info(output)
    assert (decoded['width'],decoded['height'],decoded['mips'],decoded['format']) == (*sizes[0],len(sizes),'A8R8G8B8')
    assert len(output)==128+sum(w*h*4 for w,h in sizes)
    offset=128
    for alpha in alpha_chain:
        count=alpha.width*alpha.height*4
        assert output[offset+3:offset+count:4]==alpha.tobytes()
        offset+=count
    return output

def sample(x,y,width,height):
    return int(max(0,min(width-1,x))),int(max(0,min(height-1,y)))

def stroke_brush(size,wrap,rng,lum,covered,kind):
    w,h=size
    relief_gradient=values(blur(lum,1.5,wrap))
    gy,gx=np.gradient(relief_gradient)
    luma=values(lum)
    pad=16
    canvas=Image.new('L',(w+pad*2,h+pad*2),128)
    draw=ImageDraw.Draw(canvas)
    is_bark=kind=='bark'
    repeats=w*h//(95 if is_bark else 118)
    for _ in range(repeats):
        x,y=rng.randrange(w),rng.randrange(h)
        if covered[y,x] < .60:
            continue
        dx,dy=gx[y,x],gy[y,x]
        if hypot(dx,dy)>1.0:
            angle=atan2(float(dy),float(dx))+pi*.5
        elif is_bark:
            angle=rng.uniform(1.18,1.96)
        else:
            angle=rng.uniform(0,pi)
        angle+=rng.uniform(-.21,.21)
        dx,dy=cos(angle),sin(angle)
        length=rng.uniform(3.5,13.8) if is_bark else rng.uniform(3.0,7.8)
        end=(x+dx*length,y+dy*length)
        # Leaf veins end on the same painted leaflet and inside the cutout.
        if not is_bark:
            edge_x,edge_y=sample(*end,w,h)
            if covered[edge_y,edge_x] < .7 or abs(luma[edge_y,edge_x]-luma[y,x])>14.0:
                continue
        bend=rng.uniform(-1.3,1.3)
        d=rng.randint(9,19) if is_bark else rng.randint(7,14)
        if rng.random()<.78:
            shade,light=128-d,128+max(2,d//3)
        else:
            shade,light=128+d//2,128-max(2,d//3)
        offsets_x=[0]
        offsets_y=[0]
        if wrap[0] and x<pad: offsets_x.append(w)
        if wrap[0] and x>w-pad: offsets_x.append(-w)
        if wrap[1] and y<pad: offsets_y.append(h)
        if wrap[1] and y>h-pad: offsets_y.append(-h)
        for ox in offsets_x:
            for oy in offsets_y:
                a,b=x+ox+pad,y+oy+pad
                center=(a+dx*length*.46-dy*bend,b+dy*length*.46+dx*bend)
                last=(a+dx*length,b+dy*length)
                line=((a,b),center,last)
                draw.line([(px-dy*.76,py+dx*.76) for px,py in line],fill=light)
                draw.line(line,fill=shade)
                if not is_bark:
                    for sign in (-1,1):
                        wing=(center[0]+dx*length*.23+sign*dy*length*.28,
                              center[1]+dy*length*.23-sign*dx*length*.28)
                        draw.line((center,wing),fill=128+(light-128)//2)
    layer=canvas.crop((pad,pad,w+pad,h+pad))
    softly=blur(layer,.43,wrap)
    return values(softly)-values(blur(softly,4.8,wrap))

def broad_leaf_veins(size,wrap):
    im=Image.new('L',size,128)
    draw=ImageDraw.Draw(im)
    w,h=size
    # The four broad leaves occupy these separate UV islands. The stem is between them.
    islands=[
        ((.154,.452),(.166,.078)),((.864,.446),(.841,.078)),
        ((.173,.567),(.120,.931)),((.785,.570),(.806,.932)),
    ]
    for j,(root,tip) in enumerate(islands):
        dx,dy=tip[0]-root[0],tip[1]-root[1]
        for i,t in enumerate((.24,.35,.46,.56,.67,.76)):
            u,v=root[0]+dx*t,root[1]+dy*t
            spread=.055+.031*sin(pi*t)
            for sign in (-1,1):
                start=(u*w,v*h)
                mid=((u+sign*spread*.54)*w,(v+dy*.067)*h)
                end=((u+sign*spread*(1.0 if (i+j)%2 else .86))*w,(v+dy*.16)*h)
                draw.line(((start[0]+.8,start[1]),(mid[0]+.8,mid[1]),(end[0]+.8,end[1])),fill=135)
                draw.line((start,mid,end),fill=117+(i%3)*2)
    fine=blur(im,.43,wrap)
    return values(fine)-values(blur(fine,5.2,wrap))

bark = {'branches','trunk2h','trunk3h','trunk3_10','trunk_brown','trunk_red','willowtrunk'}
mixed = {'willowq1','willowq2','willowq3','willowq4','trunkmossy',
         'treewall01','treewall01edge','treewall02','treewall02edge'}

def terrain(name, kind, stored, reference, wrap):
    old = hd.decode_top(stored, hd.dds_info(stored)).convert('RGBA')
    reference = reference.convert('RGBA')
    non_opaque=old.getextrema()[3][0] < 255
    factor=2 if max(old.size)<256 and not non_opaque else 1
    size=(old.width*factor,old.height*factor)
    base_alpha=old.getchannel('A') if non_opaque else Image.new('L',size,255)
    alpha=values(base_alpha)/255.0
    expected=weighted_rgb(values(reference.convert('RGB')),values(reference.getchannel('A'))/255.0)
    retained=resample(old.convert('RGB'),size,wrap)
    source=retained if non_opaque else resample(reference.convert('RGB'),size,wrap)
    base=values(source)
    base*=expected/np.maximum(weighted_rgb(base,alpha),1.0)

    seed=int.from_bytes(hashlib.sha256(name.encode()).digest()[:4],'little')
    rng=Random(seed)
    fine=grain(size,wrap,seed)
    if kind=='mixed':
        f=np.clip((base[...,1]-base[...,0]+6.0)/32.0,0.0,1.0)
        relief=f*painted_relief(size,wrap,rng,'grass')+(1.0-f)*painted_relief(size,wrap,rng,'soil')
    else:
        relief=painted_relief(size,wrap,rng,kind)
    hd_luma=retained.convert('L')
    inherited=values(hd_luma)-values(blur(hd_luma,1.5,wrap))
    amplitude={'grass':1.55,'tuft':.95,'mixed':1.25,'soil':1.15,'fine':.58,'stubble':1.00}[kind]
    delta=(.43*fine+amplitude*relief+.32*inherited)*np.clip((alpha-.32)/.60,0.0,1.0)

    yy,xx=np.ogrid[:size[1],:size[0]]
    taper=1.0
    if not wrap[0]: taper=taper*np.clip(np.minimum(xx,size[0]-1-xx)/8.,0.,1.)
    if not wrap[1]: taper=taper*np.clip(np.minimum(yy,size[1]-1-yy)/8.,0.,1.)
    delta=taper*(delta-(delta*alpha).sum()/alpha.sum())
    for i,axis in enumerate((1,0)):
        if wrap[i]:
            seam = abs(np.take(delta,0,axis=axis)-np.take(delta,-1,axis=axis)).mean()
            inside = abs(np.diff(delta,axis=axis)).mean()
            assert seam <= 1.8*inside+1.2, (name,axis,seam,inside)
    luma=base@np.float32([.2126,.7152,.0722])
    gain=1.0+np.clip(delta/np.maximum(luma,30.),-.16,.16)
    rgb=np.uint8(np.rint(np.minimum(base*gain[...,None],255.0)))
    palette_error=float(np.max(np.abs(weighted_rgb(rgb.astype(np.float32),alpha)-expected)))
    assert palette_error<=1.6, (name,palette_error)

    new_rgb=Image.fromarray(rgb)
    chain=get_original_alpha_mips(stored) if non_opaque else [Image.new('L',wh,255) for wh in hd.mip_sizes(*size)]
    return write_texture(new_rgb, chain)

def foliage(rel, packed, ref, wrap):
    name=Path(rel).stem.lower()
    kind='bark' if name in bark else 'mixed' if name in mixed else 'leaf'
    old = hd.decode_top(packed, hd.dds_info(packed)).convert('RGBA')
    ref = ref.convert('RGBA')
    cutout=old.getextrema()[3][0]<255
    upscale=2 if max(old.size)<256 and not cutout else 1
    size=(upscale*old.width,upscale*old.height)
    a=old.getchannel('A') if cutout else Image.new('L',size,255)
    alpha=values(a)/255.
    old_lum=old.convert('RGB').convert('L')
    retained=resample(old.convert('RGB'),size,wrap)
    base=values(retained if cutout else resample(ref.convert('RGB'),size,wrap))
    expected=weighted_rgb(values(ref.convert('RGB')),values(ref.getchannel('A'))/255.)
    base*=expected/np.maximum(weighted_rgb(base,alpha),1.)
    old_lum=retained.convert('L')
    inside=np.clip((values(a.filter(ImageFilter.MinFilter(3)))/255.-.33)/.54,0.,1.)
    seed=int.from_bytes(hashlib.sha256(rel.lower().encode()).digest()[:4],'little')
    rng=Random(seed)
    inherited=values(blur(old_lum,.61,wrap))-values(blur(old_lum,2.25,wrap))
    f_green=np.clip((base[...,1]-np.maximum(base[...,0],base[...,2])+3.0)/21.0,0,1)
    if kind=='bark':
        brush=stroke_brush(size,wrap,rng,old_lum,inside,'bark')
        delta=.66*inherited+.91*brush
    elif kind=='mixed':
        leaf=stroke_brush(size,wrap,rng,old_lum,inside*f_green,'leaf')
        grooves=stroke_brush(size,wrap,rng,old_lum,inside*(1.-f_green),'bark')
        delta=.57*inherited+.89*(leaf*f_green+grooves*(1.-f_green))
    else:
        leafy_area=inside*f_green if name=='flowers_bush' else inside
        if name=='leafyplant':
            brush=broad_leaf_veins(size,wrap)
            delta=(.49*inherited+1.29*brush)*leafy_area
        else:
            brush=stroke_brush(size,wrap,rng,old_lum,leafy_area,'leaf')
            delta=(.61*inherited+1.08*brush)*leafy_area

    delta=(delta+.23*grain(size,wrap,seed))*inside
    taper=1.
    yy,xx=np.ogrid[:size[1],:size[0]]
    if not wrap[0]: taper=taper*np.clip(np.minimum(xx,size[0]-1-xx)/8.,0.,1.)
    if not wrap[1]: taper=taper*np.clip(np.minimum(yy,size[1]-1-yy)/8.,0.,1.)
    delta=taper*(delta-inside*(delta*alpha).sum()/max((inside*alpha).sum(),1.))
    for i,axis in enumerate((1,0)):
        if wrap[i]:
            seam=abs(np.take(delta,0,axis=axis)-np.take(delta,-1,axis=axis)).mean()
            typical=abs(np.diff(delta,axis=axis)).mean()
            assert seam <= 2.25*typical+1.2, (rel,seam,typical)
    gain=1.+np.clip(delta/np.maximum(base@np.float32([.2126,.7152,.0722]),26.),-.145,.145)
    rgb=np.uint8(np.rint(np.minimum(base*gain[...,None],255.)))
    palette_error=float(np.max(np.abs(weighted_rgb(rgb.astype(np.float32),alpha)-expected)))
    assert palette_error<=1.50,(rel,palette_error)
    new=Image.fromarray(rgb)
    chain=get_original_alpha_mips(packed) if cutout else [Image.new('L',wh,255) for wh in hd.mip_sizes(*size)]
    return write_texture(new, chain)

def otis_mask(data_file):
    size = 512
    body = Image.new('L', (size,size))
    keep = body.copy()
    main = {'SIM_otis.trb','SIM_Otis_a2.trb','sim_otis_a3.trb'}
    names = ('SIM_otis.trb','SIM_Otis_a2.trb','sim_otis_a3.trb','SS_Otis.trb','ct_otis.trb',
             'end_otis.trb','nightbarn_otis.trb','SS01_Otis.trb','SSBR_Otis.trb')
    regions = {'c_pelvis','c_spine','c_spine1','c_spine2','c_neck','c_neck1'}

    def draw_wrapped(draw, uv):
        if max(u for u,v in uv)-min(u for u,v in uv) > .95 or max(v for u,v in uv)-min(v for u,v in uv) > .95:
            return False
        for tile_u in range(floor(min(u for u,v in uv)), ceil(max(u for u,v in uv))):
            for tile_v in range(floor(min(v for u,v in uv)), ceil(max(v for u,v in uv))):
                draw.polygon([((u-tile_u)*size,(v-tile_v)*size) for u,v in uv], fill=255)
        return True

    for name in names:
        doc = hd.TRB(data_file('Models/'+name).read_bytes())
        symbols = {key: (section, off) for key, section, off in doc.symbols}
        if 'Skeleton' not in symbols:
            continue
        section = doc.sec(0)

        def ptr(offset):
            target_section, target_offset = doc.ptr(0, offset)
            assert target_section == 0
            return target_offset

        skeleton = symbols['Skeleton'][1]
        bone_count, = unpack_from('<H', section, skeleton)
        bone_start = ptr(skeleton+0x34)
        coat_bones = set()
        for bone in range(bone_count):
            b = bone_start+bone*192+144
            bone_name = section[b+1:b+1+section[b]].decode('ascii').lower()
            if bone_name in regions:
                coat_bones.add(bone)

        affected = 0
        protected = 0
        for sym in symbols:
            if not sym.startswith('LOD') or '_Mesh_' not in sym:
                continue
            mesh = symbols[sym][1]
            mat = ptr(mesh+12)
            name_end = section.index(b'\0',mat)
            if section[mat:name_end].lower() != b'otis':
                continue
            num_parts, _, count = unpack_from('<III', section, mesh)
            part_base = ptr(mesh+16)
            vertex_base = ptr(part_base+20)
            vertices = [unpack_from('<6f8B2f',section,vertex_base+i*40) for i in range(count)]
            for k in range(num_parts):
                part = part_base+k*52
                index_count, palette_count = unpack_from('<II',section,part+8)
                palette = unpack_from(f'<{palette_count}I',section,ptr(part+16))
                indices = unpack_from(f'<{index_count}H',section,ptr(part+24))
                assert max(indices) < count
                coverage = {}
                for index in set(indices):
                    pt = vertices[index]
                    weight = 0
                    for w, rows in zip(pt[6:10],pt[10:14]):
                        if w:
                            assert rows % 3 == 0 and rows//3 < len(palette)
                            if palette[rows//3] in coat_bones:
                                weight += w
                    coverage[index] = weight/255.
                for a,b,c in zip(indices, indices[1:], indices[2:]):
                    if len({a,b,c}) != 3:
                        continue
                    p, q, r = (vertices[i][:3] for i in (a,b,c))
                    dx,dy,dz = (y-x for x,y in zip(p,q))
                    ex,ey,ez = (y-x for x,y in zip(p,r))
                    area = (dy*ez-dz*ey)**2 + (dz*ex-dx*ez)**2 + (dx*ey-dy*ex)**2
                    if area < 1e-13:
                        continue
                    uv = [vertices[i][14:16] for i in (a,b,c)]
                    if len(set(uv)) != 3:
                        continue
                    weights = [coverage[i] for i in (a,b,c)]
                    if min(weights) >= .42 and sum(weights) >= 1.65:
                        if name in main and sym.startswith('LOD0_'):
                            affected += draw_wrapped(ImageDraw.Draw(body), uv)
                    elif max(weights) <= .35:
                        protected += draw_wrapped(ImageDraw.Draw(keep), uv)

    mask = ImageChops.subtract(body.filter(ImageFilter.MinFilter(5)),keep.filter(ImageFilter.MaxFilter(7)))
    mask = ImageChops.multiply(mask.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(1.5)),
                               ImageChops.invert(keep.filter(ImageFilter.MaxFilter(5))))
    hist = mask.histogram()
    assert 1000 < sum(hist[160:]) < 85000
    hist = ImageChops.multiply(mask,keep).histogram()
    assert not sum(hist[1:])
    return mask

def otis(original, data_file):
    info = hd.dds_info(original)
    shape = (info['width'],info['height'])
    sizes = hd.mip_sizes(*shape)
    assert shape == (256,256) and len(sizes) == info['mips'] == 9

    mask = otis_mask(data_file)
    brush = Image.new('L', mask.size,128)
    draw = ImageDraw.Draw(brush)
    rng = Random(190447)
    starts = [(x,y) for y in range(mask.height) for x in range(mask.width) if mask.getpixel((x,y)) > 155]
    for _ in range(len(starts)*2):
        x,y = rng.choice(starts)
        length = rng.uniform(1.4,5.1)
        angle = rng.uniform(.45,1.23) + .13*sin(x*.013+y*.021)
        offset = rng.uniform(-.6,.6)
        tone = rng.choice((117,119,120,135,136,139))
        draw.line(((x,y),(x+cos(angle)*length*.56,y+sin(angle)*length*.56),
                   (x+cos(angle)*length+offset,y+sin(angle)*length)), fill=tone)
    soft = brush.filter(ImageFilter.GaussianBlur(.32))
    top_mask = mask.resize(shape,Image.Resampling.BOX)
    top_strokes = soft.resize(shape,Image.Resampling.BOX)
    amplitudes = [min(4,max(-4,round((n-128)*m/310))) for n,m in zip(top_strokes.getdata(),top_mask.getdata())]
    delta = Image.new('L',shape)
    delta.putdata([a+128 for a in amplitudes])
    assert sum(v != 0 for v in amplitudes) >= 200
    assert all(a == 0 for a,m in zip(amplitudes,top_mask.getdata()) if m == 0)

    header = bytearray(hd.dds_header('A8R8G8B8',*shape,len(sizes)))
    flags, = unpack_from('<I',header,8)
    pack_into('<I',header,8,(flags & ~0x80000) | 0x8)
    pack_into('<I',header,20,shape[0]*4)
    levels = [bytes(header)]
    old_offset = 128
    nchanged = None
    for width,height in sizes:
        fmt = info['format']
        count = hd.level_bytes(fmt,width,height)
        mip = original[old_offset:old_offset+count]
        old_offset += count
        wrapped = hd.dds_header(fmt,width,height,1)+mip
        old = hd.decode_top(wrapped,hd.dds_info(wrapped)).convert('RGBA')
        signed_tone = delta.resize((width,height),Image.Resampling.BOX)
        painted = []
        changed = 0
        for (r,g,b,a),v in zip(old.getdata(),signed_tone.getdata()):
            d = v-128
            pixels = (max(0,min(255,r+d)),max(0,min(255,g+d)),max(0,min(255,b+d)),a)
            changed += pixels != (r,g,b,a)
            painted.append(pixels)
        image = Image.new('RGBA',old.size)
        image.putdata(painted)
        if nchanged is None:
            nchanged = changed
        assert image.getchannel('A').tobytes() == old.getchannel('A').tobytes()
        levels.append(image.tobytes('raw','BGRA'))

    assert old_offset == len(original)
    payload = b''.join(levels)
    return payload
