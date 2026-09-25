#!/usr/bin/env python3
"""Write a file (or zeros) into physical memory through an uncached mapping.

/dev/mem read()/write() go through the APPS cacheable linear map, and the CDSP is
not coherent with the APPS caches: lines left dirty there can be written back
over memory the CDSP has since reused.  mmap() of /dev/mem with O_SYNC is
uncached, so the data is in DDR when the call returns.

usage: memwrite.py <phys> <file>      write the file at phys
       memwrite.py <phys> zero:<len>  zero len bytes at phys
       memwrite.py <phys> check:<file> compare memory with the file
"""
import mmap, os, sys, hashlib

PAGE = mmap.PAGESIZE
CHUNK = 32 << 20

def window(fd, phys, length, access):
    base = phys & ~(PAGE - 1)
    off = phys - base
    m = mmap.mmap(fd, off + length, mmap.MAP_SHARED, access, offset=base)
    return m, off

def main():
    phys = int(sys.argv[1], 0)
    what = sys.argv[2]
    fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    if what.startswith("zero:"):
        total = int(what[5:], 0)
        z = bytes(CHUNK)
        done = 0
        while done < total:
            n = min(CHUNK, total - done)
            m, off = window(fd, phys + done, n, mmap.PROT_WRITE)
            m[off:off + n] = z[:n]
            m.close()
            done += n
        print("zeroed %#x bytes at %#x" % (total, phys))
        return 0
    check = what.startswith("check:")
    path = what[6:] if check else what
    data = open(path, "rb").read()
    bad = 0
    done = 0
    while done < len(data):
        n = min(CHUNK, len(data) - done)
        m, off = window(fd, phys + done, n, mmap.PROT_READ if check else mmap.PROT_READ | mmap.PROT_WRITE)
        if check:
            if m[off:off + n] != data[done:done + n]:
                bad += 1
        else:
            m[off:off + n] = data[done:done + n]
        m.close()
        done += n
    if check:
        print("%s: %s" % (path, "MISMATCH" if bad else "ok"))
        return 1 if bad else 0
    print("wrote %d bytes of %s at %#x" % (len(data), path, phys))
    return 0

sys.exit(main())
