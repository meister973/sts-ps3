#!/usr/bin/env python3
"""Recursively upload a local directory to the Vita over VitaShell's FTP.

Used by `make deploy-assets` to place the processed asset payload at
ux0:data/stsvita/assets for the slim (code-only) VPK, which reads assets from
ux0: instead of the VPK's app0: mount. Enable FTP in VitaShell with SELECT;
the default port is 1337. Dependency-free (stdlib ftplib).

  tools/deploy_assets.py --src work/assets_vita --host 192.168.1.42 \
      --dest ux0:data/stsvita/assets
"""
import argparse
import ftplib
import sys
from pathlib import Path


def ensure_dir(ftp: ftplib.FTP, path: str) -> None:
    """mkdir -p for FTP: create each path component, ignoring 'exists'."""
    parts, cur = path.split("/"), ""
    for p in parts:
        if not p:
            continue
        cur = f"{cur}/{p}" if cur else p
        try:
            ftp.mkd(cur)
        except ftplib.error_perm:
            pass  # already exists


def remote_size(ftp: ftplib.FTP, path: str) -> int | None:
    try:
        size = ftp.size(path)
    except ftplib.all_errors:
        return None
    return int(size) if size is not None else None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", required=True, help="local directory to upload")
    ap.add_argument("--host", required=True, help="Vita IP (VitaShell FTP)")
    ap.add_argument("--port", type=int, default=1337)
    ap.add_argument("--dest", required=True, help="remote base dir, e.g. ux0:data/stsvita/assets")
    ap.add_argument("--force", action="store_true", help="upload files even when remote size matches")
    args = ap.parse_args()

    src = Path(args.src)
    if not src.is_dir():
        print(f"not a directory: {src}", file=sys.stderr)
        return 2

    files = [p for p in src.rglob("*") if p.is_file()]
    total = len(files)
    print(f"uploading {total} files from {src} -> {args.host}:{args.port}/{args.dest}")

    ftp = ftplib.FTP()
    ftp.connect(args.host, args.port, timeout=30)
    ftp.login()
    try:
        ftp.voidcmd("TYPE I")
    except ftplib.all_errors:
        pass
    made = {""}
    uploaded = skipped = 0
    for i, f in enumerate(files, 1):
        rel = f.relative_to(src).as_posix()
        remote_path = f"{args.dest}/{rel}"
        remote_dir = remote_path.rsplit("/", 1)[0]
        if remote_dir not in made:
            ensure_dir(ftp, remote_dir)
            made.add(remote_dir)
        if not args.force and remote_size(ftp, remote_path) == f.stat().st_size:
            skipped += 1
        else:
            with f.open("rb") as fh:
                ftp.storbinary(f"STOR {remote_path}", fh)
            uploaded += 1
        if i % 100 == 0 or i == total:
            print(f"  {i}/{total} (uploaded {uploaded}, skipped {skipped})")
    ftp.quit()
    print(f"done: uploaded {uploaded}, skipped {skipped}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
