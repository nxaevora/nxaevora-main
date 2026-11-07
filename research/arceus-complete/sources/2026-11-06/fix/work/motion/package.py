from pathlib import Path
import hashlib, json, zipfile
here=Path(__file__).resolve().parent
root=here.parents[1];out=root/'outputs'
files={
    'taa':['build.py','integration.inc','resolve.comp','resolve.spv','temporal_camera.h','taa_words.h','check_gpu.c'],
    'ups':['build.py','split.inc','surface.c','prepare.comp','prepare.spv','ngx.c','fsr.cpp','vk_dlaa.h','vk_fsr2.h'],
    'motion':['build_shaders.py','build_replay.py','material_flow.h','capture.inc','cut_frame.h','common.glsl',
              'seed.comp','measure.comp','candidates.comp','score.comp','choose.comp','validate.comp','apply.comp',
              'flow_words.h','replay_io.h','replay_device.h','replay_gpu.c','temporal_replay.c','actor_replay.py',
              'fractional_replay.py','affine_test.py','integration_test.py','continuous_test.py',
              'cut_isolation.py','inspect_capture.py','check_snapshot.py','benchmark.py']
}
archive=out/'Temporal-Renderer-Sources.zip'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as z:
    z.write(here/'source-notes.txt','README.txt')
    z.write(here/'kernel-timing.json','results/kernel-timing.json')
    for folder,items in files.items():
        for item in items:
            f=root/'work'/folder/item;z.write(f,f.relative_to(root))
    for folder,items in {'fixtures':['metrics.json','timings.tsv'],'fractional':['metrics.json'],
            'deformation':['metrics.json'],'integrated':['metrics.json','capture-check.json'],
            'continuous':['metrics.json','cut-isolation.json']}.items():
        for item in items:z.write(here/folder/item,'results/'+folder+'/'+item)
with zipfile.ZipFile(archive) as z:assert z.testzip() is None

metrics=json.loads((here/'continuous/metrics.json').read_text())
lines=['Arceus temporal rendering tests','', 'Playable test builds',
       'Arceus-TAA-Test.exe resolves the 2560 x 1440 scene before the HUD.',
       'Arceus-DLSS-Test.exe and Arceus-FSR-Test.exe reconstruct a 1920 x 1080 scene at 2560 x 1440. The AMD runtime selected FSR 3.1.6.',
       'All three carry the neutral-rim change previously checked in both crafting-box screenshots. The stock game executable stays installed.',
       '', 'Controlled Vulkan replay on the RTX 5090',
       'A character from a saved capture was moved independently across another captured scene through a walk, stop, reversal and cut. The camera also panned and the sampling jitter changed each frame.',
       'The metric is the mean absolute display-mapped RGB difference on character pixels across five sampled frames. Each backend is compared with its own output using the known motion. Lower is closer.',
       '', 'Backend     Camera only     Candidate     Reduction']
for name in ('TAA','DLSS','FSR'):
    row=metrics[name]['mean_actor_error'];a=row['camera'];b=row['candidate']
    lines.append(f'{name:8}    {a:.6f}       {b:.6f}      {100*(1-b/a):.1f}%')
bench=json.loads((here/'kernel-timing.json').read_text())
lines+=['','Isolated estimator timing on the RTX 5090']
for size,label in [('1920','1920 x 1080 motion pass'),('2560','2560 x 1440 motion pass including native camera-vector generation')]:
    r=bench[size];lines.append(f"{label} took {r['median_gpu_ms']:.3f} ms at the median and {r['p95_gpu_ms']:.3f} ms at the 95th percentile.")
lines.append('GPU timestamps span the estimator in 40 warm moving-character samples. Uploads, vendor reconstruction and the rest of the game are outside this timing.')
lines+=['','The stationary jitter and camera-pan controls had zero measured false background corrections larger than one input pixel. Two actors sharing materials and moving in opposite directions reached 99.77% within one input pixel. About 0.008% of the unchanged background received a false correction in that case.',
        'Fast turns are harder. On the 12-degree rotation test the mean character-vector error fell from 43.9 to 24.2 input pixels. Motion beyond the 64-pixel search radius falls back to camera reprojection with reduced history confidence where matching fails.',
        '', 'History resets',
        'NGX evaluates the new scene twice on a reset. Comparing identical new frames after two different histories gave identical output on the cut and on the following three frames. Ordinary frames use one NGX evaluation. Native TAA and FSR also passed their reset comparisons.',
        '', 'In-game verification',
        'The game stayed closed during these tests. Live character animation, HUD appearance and gameplay frame times still need observation. The renderer estimates opaque object motion from camera, depth, material IDs and previous color. No game-supplied skeletal or particle motion buffer has been confirmed.',
        'Open a test launcher and press F3 while the player or an NPC moves. Save and close normally. The screenshot includes current/previous packed scene features, the estimated vectors, the confidence mask and a temporal manifest when adjacent material history is available.',
        '', 'Runtime SHA-256']
for folder in ('taa-runtime','upscale-runtime'):
    f=out/folder/'ArceusTest.exe'
    lines.append(folder+'/ArceusTest.exe    '+hashlib.sha256(f.read_bytes()).hexdigest())
(out/'Temporal-Test-Results.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
