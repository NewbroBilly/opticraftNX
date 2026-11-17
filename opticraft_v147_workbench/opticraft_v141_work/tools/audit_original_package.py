#!/usr/bin/env python3
import hashlib,sys,zipfile
from pathlib import PurePosixPath
archive=sys.argv[1] if len(sys.argv)>1 else "/mnt/data/OptiCraft JAVA LITE 1.2.5.zip"
expected={
 "minecraft.jar":"OptiCraft JAVA LITE 1.2.5/mcdata/bin/minecraft.jar",
 "lwjgl.jar":"OptiCraft JAVA LITE 1.2.5/mcdata/bin/lwjgl.jar",
 "lwjgl_util.jar":"OptiCraft JAVA LITE 1.2.5/mcdata/bin/lwjgl_util.jar",
 "jinput.jar":"OptiCraft JAVA LITE 1.2.5/mcdata/bin/jinput.jar",
}
with zipfile.ZipFile(archive) as outer:
    names=set(outer.namelist())
    for label,path in expected.items():
        if path not in names: raise SystemExit("missing "+path)
        data=outer.read(path)
        with zipfile.ZipFile(__import__("io").BytesIO(data)) as jar:
            entries=jar.namelist()
            classes=sum(n.endswith(".class") for n in entries)
            if label=="minecraft.jar" and "net/minecraft/client/Minecraft.class" not in entries:
                raise SystemExit("Minecraft main class absent")
            print("%s bytes=%d sha256=%s entries=%d classes=%d"%
                  (label,len(data),hashlib.sha256(data).hexdigest(),len(entries),classes))
    windows=[n for n in names if n.lower().endswith((".dll",".exe"))]
    print("Windows-only binaries in source archive:",len(windows))
    print("Original JAR files are verified as ZIP archives; no files modified.")
