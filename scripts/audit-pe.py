"""Inspect this project's PE imports without third-party Python modules.

This checks the subsystem, runtime DLLs and a known post-Win7 API denylist.
It is a build audit, not a replacement for testing on Windows 7 hardware/VMs.
"""
import argparse
import json
from pathlib import Path
import struct

p=argparse.ArgumentParser()
p.add_argument('file',type=Path)
p.add_argument('--output',type=Path)
args=p.parse_args()
data=args.file.read_bytes()
u16=lambda o:struct.unpack_from('<H',data,o)[0]
u32=lambda o:struct.unpack_from('<I',data,o)[0]
pe=u32(0x3c)
if data[:2]!=b'MZ' or data[pe:pe+4]!=b'PE\0\0':
    raise SystemExit('Not a PE file')
machine=u16(pe+4)
count=u16(pe+6)
optional=pe+24
magic=u16(optional)
is64=magic==0x20b
sections=[]
for i in range(count):
    offset=optional+u16(pe+20)+40*i
    virtual_size,rva,raw_size,raw=struct.unpack_from('<IIII',data,offset+8)
    sections.append((rva,max(virtual_size,raw_size),raw))
def file_offset(rva):
    for begin,size,raw in sections:
        if begin<=rva<begin+size:
            return raw+rva-begin
    if rva<u32(optional+60):
        return rva
    raise ValueError(f'Unmapped RVA {rva:x}')
def cstring(rva):
    offset=file_offset(rva)
    return data[offset:data.index(0,offset)].decode('ascii')
directory=optional+(112 if is64 else 96)
imports={}
cursor=file_offset(u32(directory+8))
for _ in range(256):
    original,_,_,name,first=struct.unpack_from('<IIIII',data,cursor)
    if not name:
        break
    dll=cstring(name)
    imports[dll]=[]
    thunk=file_offset(original or first)
    width=8 if is64 else 4
    for i in range(16384):
        value=struct.unpack_from('<Q' if is64 else '<I',data,thunk+i*width)[0]
        if not value:
            break
        if value & (1 << (width*8-1)):
            imports[dll].append(f'ordinal:{value & 65535}')
        else:
            imports[dll].append(cstring(value+2))
    cursor+=20
allowed={'advapi32.dll','bcrypt.dll','comctl32.dll','comdlg32.dll','crypt32.dll','fwpuclnt.dll','gdi32.dll',
         'iphlpapi.dll','kernel32.dll','msvcrt.dll','ntdll.dll','ole32.dll','oleaut32.dll','rpcrt4.dll',
         'secur32.dll','shell32.dll','user32.dll','ws2_32.dll','winmm.dll','winspool.drv'}
post_win7={'GetDpiForWindow','SetProcessDpiAwareness','SetProcessDpiAwarenessContext','GetSystemTimePreciseAsFileTime',
           'GetTempPath2W','GetTempPath2A','WaitOnAddress','WakeByAddressSingle','WakeByAddressAll',
           'SetThreadDescription','GetThreadDescription','GetThreadInformation','SetThreadInformation',
           'IsWow64Process2','CreateFile2','GetAddrInfoExCancel','SetInterfaceDnsSettings','GetInterfaceDnsSettings'}
unexpected=[dll for dll in imports if dll.lower() not in allowed]
new_apis=[name for names in imports.values() for name in names if name in post_win7]
version=[u16(optional+48),u16(optional+50)]
result={'file':str(args.file),'architecture':'x64' if machine==0x8664 else 'x86' if machine==0x14c else hex(machine),
        'subsystem_version':version,'system_dlls_only':not unexpected,'unexpected_dlls':unexpected,
        'known_post_win7_imports':new_apis,'imports':imports,
        'passed':version==[6,1] and not unexpected and not new_apis and 'msvcrt.dll' in {s.lower() for s in imports}}
if args.output:
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2,ensure_ascii=False),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='imports'},ensure_ascii=False))
print('DLLs: '+', '.join(imports))
raise SystemExit(0 if result['passed'] else 1)
