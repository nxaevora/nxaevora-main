from pathlib import Path
import json, hashlib, sys

def package_files(stage, terrain):
    directory = stage/"world-pt-v2-runtime"
    files = {name: directory/name for name in ("ArceusTest.exe","nvngx_dlss.dll","amd_fidelityfx_loader_vk.dll","amd_fidelityfx_upscaler_vk.dll")}
    files.update({"terrain/"+file.name:file for file in terrain.iterdir() if file.is_file()})
    hashes={}
    for name,path in sorted(files.items()):
        with path.open("rb") as stream:
            hashes[name]=hashlib.file_digest(stream,"sha256").hexdigest()
    return hashes

if __name__=="__main__":
    stage, terrain, output = map(Path, sys.argv[1:4])
    output.write_text(json.dumps(package_files(stage,terrain),indent=2)+"\n")
