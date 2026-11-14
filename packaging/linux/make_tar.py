#!/usr/bin/env python3
"""把 linux-x86_64 目录打成 tar.gz，保持 Unix 权限（Windows 上没有 chmod / tar --mode 不可靠）。

用法：python make_tar.py <src_dir> <folder_name> <out.tar.gz>
"""
import os
import sys
import tarfile

EXEC_MODE = {"qingxi-notepad", "install.sh"}


def main() -> int:
    src, folder, out = sys.argv[1], sys.argv[2], sys.argv[3]
    if not os.path.isdir(src):
        print("src dir missing: " + src, file=sys.stderr)
        return 2

    with tarfile.open(out, "w:gz", compresslevel=9) as tf:
        for name in sorted(os.listdir(src)):
            path = os.path.join(src, name)
            if not os.path.isfile(path):
                continue
            mode = 0o755 if name in EXEC_MODE else 0o644
            info = tf.gettarinfo(path, arcname=f"{folder}/{name}")
            info.mode = mode
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            info.mtime = 1794658000
            with open(path, "rb") as fh:
                tf.addfile(info, fh)
    print(f"{out} {os.path.getsize(out)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
