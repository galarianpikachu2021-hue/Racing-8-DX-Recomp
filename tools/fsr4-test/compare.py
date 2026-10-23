# SPDX-License-Identifier: GPL-3.0-or-later
# Compares upscaler outputs for dumped frames: mean edge energy ("sharpness") and PSNR against a reference.
#   compare.py <dir with out_NNNN.raw (A2B10G10R10)> [<reference dir with fsr4_NNNN.raw (RGBA16F)>]
import sys, numpy as np
def lum(a): return a@np.array([0.299,0.587,0.114],dtype=np.float32)
def sharp(a):
    l=lum(a); gx=np.diff(l,axis=1); gy=np.diff(l,axis=0); return float(np.sqrt((gx[:-1,:]**2+gy[:,:-1]**2)).mean())
def p2(path):
    o=np.fromfile(path,dtype=np.uint32).reshape(1440,2560)
    return np.stack([(o&0x3ff)/1023,((o>>10)&0x3ff)/1023,((o>>20)&0x3ff)/1023],-1)
def h16(path): return np.clip(np.fromfile(path,dtype=np.float16).reshape(1440,2560,4)[:,:,:3].astype(np.float32),0,1)
d=sys.argv[1]; ref=sys.argv[2] if len(sys.argv)>2 else 'out_3.1.5'
fr=range(40,110,10)
sh=[];ps=[]
for n in fr:
    a=p2('%s/out_%04d.raw'%(d,n)); r=h16('%s/fsr4_%04d.raw'%(ref,n))
    sh.append(sharp(a)); ps.append(10*np.log10(1/max(((a-r)**2).mean(),1e-9)))
print('%-12s sharpness %.4f  PSNR vs %s %.2f dB'%(d,np.mean(sh),ref,np.mean(ps)))
