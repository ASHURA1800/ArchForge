#!/usr/bin/env python3
"""Forensically inspect the FAT filesystem inside the HDD image.
Limine finds limine.conf but cannot open the kernel -> check FAT structure,
root dir entries, LFN records, and the kernel's FAT cluster chain."""
import struct, sys

img = sys.argv[1] if len(sys.argv) > 1 else 'build/archforge.img'
with open(img, 'rb') as f:
    data = f.read()

print(f"Image size: {len(data)} bytes\n")

# MBR partition table
print("MBR partitions:")
parts = []
for i in range(4):
    e = data[446 + 16*i : 446 + 16*(i+1)]
    if all(b == 0 for b in e):
        continue
    status, ptype = e[0], e[4]
    lba_start, size = struct.unpack('<II', e[8:16])
    parts.append((i, status, ptype, lba_start, size))
    print(f"  Part {i}: status=0x{status:02x} type=0x{ptype:02x} start_lba={lba_start} size={size} sectors")

if not parts:
    print("NO PARTITIONS FOUND — image may be raw FAT (superfloppy)")
    part_lba = 0
else:
    part_lba = parts[0][3]

off = part_lba * 512
bs = data[off:off+90]
print(f"\nBoot sector at offset {off}:")
print(f"  Jump: {bs[0]:02x} {bs[1]:02x} {bs[2]:02x}  OEM: {bs[3:11]}")

bpb = {
    'bytes_per_sector': struct.unpack('<H', bs[11:13])[0],
    'sectors_per_cluster': bs[13],
    'reserved_sectors': struct.unpack('<H', bs[14:16])[0],
    'num_fats': bs[16],
    'root_entries': struct.unpack('<H', bs[17:19])[0],
    'total_sectors_16': struct.unpack('<H', bs[19:21])[0],
    'media': bs[21],
    'fat_size_16': struct.unpack('<H', bs[22:24])[0],
}
bpb['fat_size_32'] = struct.unpack('<I', bs[36:40])[0]
bpb['total_sectors_32'] = struct.unpack('<I', bs[32:36])[0]
for k, v in bpb.items():
    print(f"  {k} = {v}")

fat_size = bpb['fat_size_16'] or bpb['fat_size_32']
total = bpb['total_sectors_16'] or bpb['total_sectors_32']
is_fat32 = bpb['fat_size_16'] == 0
print(f"\nFAT type: {'FAT32' if is_fat32 else 'FAT16/12'}")

bsize = bpb['bytes_per_sector']
fat_off = off + bpb['reserved_sectors'] * bsize
root_entries = bpb['root_entries']

if is_fat32:
    root_cluster = struct.unpack('<I', bs[44:48])[0]
    data_start = fat_off + bpb['num_fats'] * fat_size * bsize
    def cluster_off(c):
        return data_start + (c - 2) * bpb['sectors_per_cluster'] * bsize
    def fat_read(c):
        return struct.unpack('<I', data[fat_off + c*4 : fat_off + c*4 + 4])[0] & 0x0FFFFFFF
    EOF = 0x0FFFFFF8
    print(f"Root cluster: {root_cluster}, data region at {data_start}")
else:
    root_off = fat_off + bpb['num_fats'] * fat_size * bsize
    data_start = root_off + root_entries * 32
    root_cluster = None
    def cluster_off(c):
        return data_start + (c - 2) * bpb['sectors_per_cluster'] * bsize
    def fat_read(c):
        return struct.unpack('<H', data[fat_off + c*2 : fat_off + c*2 + 2])[0] & 0xFFFF
    EOF = 0xFFF8
    print(f"Root dir at {root_off} ({root_entries} entries), data region at {data_start}")

def parse_entry(ent):
    if ent[0] == 0x00:
        return None
    if ent[0] == 0xE5:
        return {'deleted': True}
    attr = ent[11]
    if attr == 0x0F:
        seq = ent[0] & 0x1F
        part = ent[1:11] + ent[12:26] + ent[28:32]
        text = part.decode('utf-16-le', 'replace').split('\xff')[0]
        return {'lfn': True, 'seq': seq, 'text': text}
    name = ent[0:8].decode('ascii', 'replace').strip()
    ext = ent[8:11].decode('ascii', 'replace').strip()
    case = ent[12] if len(ent) > 12 else 0
    clus = struct.unpack('<H', ent[26:28])[0]
    if is_fat32:
        clus |= struct.unpack('<H', ent[20:22])[0] << 16
    size = struct.unpack('<I', ent[28:32])[0]
    return {'name': f"{name}.{ext}" if ext else name, 'attr': attr, 'case': case,
            'cluster': clus, 'size': size, 'raw': ent.hex()}

def walk_dir(dir_off=None, dir_cluster=None, label=''):
    entries = []
    i = 0
    while True:
        if dir_cluster is not None:
            ent = data[cluster_off(dir_cluster) + 32*i : cluster_off(dir_cluster) + 32*(i+1)]
            i += 1
            if i >= bpb['sectors_per_cluster'] * bsize // 32:
                break
        else:
            if i >= root_entries:
                break
            ent = data[dir_off + 32*i : dir_off + 32*(i+1)]
            i += 1
        if ent == b'\x00' * 32:
            break
        p = parse_entry(ent)
        if p is None:
            break
        entries.append(p)
    return entries

print(f"\n=== Root directory ===")
if is_fat32:
    ents = walk_dir(dir_cluster=root_cluster)
else:
    ents = walk_dir(dir_off=root_off)

lfn_buf = {}
kern_cluster = None
for p in ents:
    if 'deleted' in p:
        print("  [DELETED]")
        continue
    if p.get('lfn'):
        lfn_buf[p['seq']] = p['text']
        print(f"  [LFN seq {p['seq']}] {p['text']!r}")
        continue
    lfn = ''.join(lfn_buf[s] for s in sorted(lfn_buf)) if lfn_buf else ''
    case_note = ''
    if p['case'] & 0x08: case_note += ' [name-lower]'
    if p['case'] & 0x10: case_note += ' [ext-lower]'
    print(f"  {p['name']:<14} attr=0x{p['attr']:02x} cluster={p['cluster']} size={p['size']}{case_note} lfn={lfn!r}")
    if lfn:
        print(f"      raw: {p['raw']}")
    if p['name'].upper().startswith('KERNEL') or lfn.lower().endswith('kernel.elf') or p['name'].upper().startswith('K'):
        kern_cluster = p['cluster']
    if lfn.lower() == 'limine.conf' or p['name'].upper().startswith('LIMINE'):
        pass
    lfn_buf = {}

if kern_cluster and kern_cluster >= 2:
    print(f"\n=== FAT chain for kernel (start cluster {kern_cluster}) ===")
    chain = []
    c = kern_cluster
    hops = 0
    while c >= 2 and c < EOF and hops < 200:
        chain.append(c)
        c = fat_read(c)
        hops += 1
    print(f"  chain: {chain}")
    print(f"  next after last: 0x{c:X} (next raw = {c})")
    first_bytes = data[cluster_off(chain[0]):cluster_off(chain[0]) + 16]
    print(f"  first 16 bytes at cluster {chain[0]}: {first_bytes.hex()}")
    clusters_needed = (17928 + bpb['sectors_per_cluster'] * bsize - 1) // (bpb['sectors_per_cluster'] * bsize)
    print(f"  clusters needed for 17928 bytes: {clusters_needed}, chain has: {len(chain)}")
else:
    print(f"\nNo kernel cluster found (kern_cluster={kern_cluster})")

# Also check limine.conf chain for comparison
print("\n=== limine.conf check ===")
for p in ents:
    if not p.get('lfn') and not p.get('deleted') and (p['name'].upper().startswith('LIMINE') or 'CON' in p['name'].upper()):
        pass
# find LIMINE~1.CON
conf_cluster = None
for p in ents:
    if p.get('deleted') or p.get('lfn'):
        continue
    if p['name'].upper().startswith('LIMINE~') and p['name'].upper().endswith('.CON') or p['name'].upper() == 'LIMINE.CON':
        conf_cluster = p['cluster']
        break
if conf_cluster and conf_cluster >= 2:
    c = conf_cluster
    chain = []
    hops = 0
    while c >= 2 and c < EOF and hops < 50:
        chain.append(c)
        c = fat_read(c)
        hops += 1
    print(f"  limine.conf chain: {chain}, next: 0x{c:X}")
    print(f"  content preview: {data[cluster_off(chain[0]):cluster_off(chain[0])+85].decode('latin1','replace')!r}")
