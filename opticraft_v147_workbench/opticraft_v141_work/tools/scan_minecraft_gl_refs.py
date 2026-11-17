#!/usr/bin/env python3
"""List constant-pool references from Minecraft classes to LWJGL OpenGL methods."""
import sys, zipfile, struct, collections, csv

def parse(data):
    p = 0
    def u1():
        nonlocal p; x=data[p]; p+=1; return x
    def u2():
        nonlocal p; x=struct.unpack_from(">H",data,p)[0]; p+=2; return x
    def u4():
        nonlocal p; x=struct.unpack_from(">I",data,p)[0]; p+=4; return x
    if u4()!=0xCAFEBABE: return []
    u2();u2(); n=u2(); cp=[None]*n; i=1
    while i<n:
        t=u1()
        if t==1:
            ln=u2(); cp[i]=("U",data[p:p+ln].decode("utf8","replace")); p+=ln
        elif t in (3,4): p+=4
        elif t in (5,6): p+=8; i+=1
        elif t in (7,8,16,19,20): cp[i]=(t,u2())
        elif t in (9,10,11,12,17,18): cp[i]=(t,u2(),u2())
        elif t==15: p+=3
        else: raise ValueError("constant pool tag %d"%t)
        i+=1
    def u(i): return cp[i][1] if cp[i] and cp[i][0]=="U" else ""
    out=collections.Counter()
    for x in cp:
        if x and x[0] in (10,11):
            c=cp[x[1]]; nt=cp[x[2]]
            owner=u(c[1]) if c and c[0]==7 else ""
            if owner.startswith("org/lwjgl/opengl/"):
                out[(owner.replace("/","."),u(nt[1]),u(nt[2]))]+=1
    return out

if len(sys.argv)!=3:
    raise SystemExit("usage: scan_minecraft_gl_refs.py minecraft.jar output.csv")
counts=collections.Counter()
with zipfile.ZipFile(sys.argv[1]) as z:
    for name in z.namelist():
        if name.endswith(".class"): counts.update(parse(z.read(name)))
with open(sys.argv[2],"w",newline="",encoding="utf-8") as f:
    w=csv.writer(f); w.writerow(["class","method","descriptor","references"])
    for (c,m,d),n in sorted(counts.items()): w.writerow([c,m,d,n])
print("GL-related method signatures:",len(counts))
