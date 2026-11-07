from pathlib import Path
p=Path("work/assets/export-scenery.py");s=p.read_text()
s=s.replace('def export(src,dest,area,models):','def export(src,dest,area,models,tilted=False):')
a=s.index('    filtered=');b=s.index('    pack=',a)
s=s[:a]+'''    filtered={}
    for name,instances in models.items():
        rock=name.startswith('ground_') and any(k in name for k in ('rock','stone','fallentree'))
        additional=(name.startswith('ground_') and any(k in name for k in ('cliff','stump','cave'))) or name.startswith(('objects_stairs','objects_stonepaving','objects_stonepillar'))
        if not rock and not (tilted and additional):continue
        filtered[name]=[row for row in instances if not any(row[3]) and (tilted or np.max(abs(row[1][[0,2]]))<.001)]
'''+s[b:]
s=s.replace("tr=scene['transform'](x,angles,scale)","tr=scene['transform'](x,angles,scale,'zxy' if tilted else 'xyz')")
s=s.replace("src,dst=map(Path,sys.argv[1:]);","src,dst=map(Path,sys.argv[1:3]);").replace("export(src,dst,area,models)","export(src,dst,area,models,'--tilted-static' in sys.argv)")
p.write_text(s)
