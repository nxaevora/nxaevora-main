from pathlib import Path
import struct

def name_hash(name):
    # Game Freak's FNV offset basis differs from the standard FNV-1a constant.
    value=0xcbf29ce484222645
    for char in name.encode('ascii'):
        value=((value^char)*0x100000001b3)&0xffffffffffffffff
    return value

def directory_index(path):
    with Path(path).open('rb') as file:
        header=file.read(40)
        if header[:8]!=b'GFLXPACK':raise ValueError('Invalid archive')
        count,folders=struct.unpack_from('<II',header,16)
        offsets=struct.unpack('<'+'Q'*folders,file.read(8*folders))
        by_name={}
        for folder in offsets:
            file.seek(folder)
            _,items,_=struct.unpack('<QII',file.read(16))
            if items>count:raise ValueError('Invalid folder length')
            for _ in range(items):
                hash_value,index,_=struct.unpack('<QII',file.read(16))
                if index>=count:raise ValueError('Invalid archive member')
                by_name.setdefault(hash_value,[]).append(index)
        return by_name
