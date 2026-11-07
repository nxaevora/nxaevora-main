import sys
sys.path.pop(0)
from pathlib import Path
import runpy, math
summarize=runpy.run_path('outputs/summarize-arceus-research.py')['summarize']
p=Path('work/research-summary-fixture.log')
p.write_text('''arceus-engine-clock: observation probe installed kind=0
arceus-engine-clock: kind=0 count=1 ns=10 object=abc caller=main+b35b18 before=1 after=1 seconds=0.008333333 flags=0
arceus-engine-clock: kind=0 count=2 ns=20 object=abc caller=main+b35b18 before=2 after=2 seconds=0.066666666 flags=0
arceus-engine-clock: kind=0 count=3 ns=30 object=def caller=main+b39200 before=0 after=0 seconds=0 flags=0
arceus-engine-clock: kind=0 count=4 ns=40 object=def caller=main+b39200 before=1 after=1 seconds=nan flags=0
arceus-engine-clock: kind=1 count=12000 ns=50 object=123 caller=main+aa before=15 after=14 seconds=0 flags=0
arceus-physics-step: count=300 ns=60 object=456 caller=main+2af2ab8 step_seconds=0.0333333 raw_word4=0 raw_word8=1
arceus-physics-solver: count=300 ns=60 object=456 input_seconds=0.0333333 stored_seconds=0.0333333 inverse_seconds=30 substep_seconds=0.00833333 substeps=4 alternate_world=0
arceus-physics-solver: count=600 ns=90 object=456 input_seconds=0.00833333 stored_seconds=0.0333333 inverse_seconds=30 substep_seconds=0.00833333 substeps=4 alternate_world=0
arceus-physics-solver: count=900 ns=100 object=789 input_seconds=0.00833333 stored_seconds=nan inverse_seconds=nan substep_seconds=nan substeps=0 alternate_world=1
arceus-physics-simulation: count=300 ns=60 object=456 simulation=123 vtable=789 solve_entry=abc tag=0 path=single alternate_world=0
arceus-physics-simulation: count=600 ns=90 object=456 simulation=123 vtable=789 solve_entry=abc tag=1 path=multi alternate_world=0
arceus-physics-simulation: count=900 ns=100 object=789 simulation=321 vtable=987 solve_entry=def tag=99 path=unknown alternate_world=1
arceus-physics-simulation: count=1200 ns=110 object=456 simulation=654 vtable=987 solve_entry=def tag=99 path=unknown alternate_world=0
arceus-frame-timer: kind=0 count=50 changed=5 ns=120 object=222 caller=main+111 state=1 before=4 after=5
arceus-frame-timer: kind=2 count=300 changed=120 ns=130 object=333 caller=main+444 state=1 before=0.1 after=0.108333
arceus-ui-clock: count=1 ns=140 object=555 channel=1 mode=1 duration_ns=8333333 frame_ns=33333333
arceus-ui-clock: count=600 ns=150 object=666 channel=1 mode=-1 duration_ns=8333333 frame_ns=0
arceus-sequence-clock: kind=0 count=1 ns=160 object=777 caller=main+888 transition=0 state_before=3 state_after=3 mode_before=0 mode_after=0 duration_before=30 duration_after=30 before=4 after=5
arceus-sequence-clock: kind=0 count=2 ns=170 object=777 caller=main+888 transition=1 state_before=3 state_after=1 mode_before=0 mode_after=0 duration_before=30 duration_after=0 before=30 after=0
arceus-sequence-clock: kind=1 count=3 ns=180 object=999 caller=main+aaa transition=0 state_before=3 state_after=3 mode_before=0 mode_after=0 duration_before=20 duration_after=20 before=15 after=14
arceus-sequence-clock: kind=1 count=4 ns=190 object=999 caller=main+aaa transition=1 state_before=3 state_after=5 mode_before=0 mode_after=0 duration_before=20 duration_after=30 before=1 after=29
arceus-sequence-clock: kind=1 count=5 ns=200 object=bbb caller=main+aaa transition=0 state_before=8 state_after=8 mode_before=0 mode_after=0 duration_before=15 duration_after=15 before=15 after=15
arceus-engine-clock: refused instruction/function guard kind=2
''')
r=summarize(p)
s=r['engine_clocks']['script_frames']
assert s['sampled_calls']==4 and s['sampled_objects']==2 and len(s['callers'])==2
assert math.isclose(s['frames_per_elapsed_second_range'][0],30,rel_tol=1e-6)
assert math.isclose(s['frames_per_elapsed_second_range'][1],120,rel_tol=1e-6)
assert r['engine_clocks']['ride_blink']['highest_call_count']==12000
assert r['engine_clocks']['ride_blink_subobject']['sampled_calls']==0
assert r['physics_step']['highest_call_count']==300
assert r['physics_step']['step_seconds_range']==[.0333333,.0333333]
assert r['physics_solver']['normal_world_samples']==2
assert r['physics_solver']['alternate_world_samples']==1
assert r['physics_solver']['input_storage_mismatches']==1
assert r['physics_solver']['substeps']==[4]
assert r['physics_simulation']['normal_world_paths']==['multi','single','unknown']
assert r['physics_simulation']['normal_world_samples_by_path']=={'multi':1,'single':1,'unknown':1}
assert r['physics_simulation']['alternate_world_samples']==1
assert s['per_caller']['main+b35b18']['samples']==2
assert r['frame_timers']['camera_recenter']['delta_range']==[1,1]
assert r['frame_timers']['throwable_state_1']['states']==[1]
assert r['frame_timers']['throwable_state_1']['highest_change_count']==120
assert r['frame_timers']['throwable_state_2']['delta_range'] is None
assert r['ui_animation_clock']['modes']==[-1,1]
assert r['ui_animation_clock']['unknown_denominator_samples']==1
assert math.isclose(r['ui_animation_clock']['normalized_progress_range'][0],.25,rel_tol=1e-6)
assert r['sequence_clocks']['common_fade']['same_stage_delta_range']==[1,1]
assert r['sequence_clocks']['common_fade']['sampled_transitions']==1
assert r['sequence_clocks']['capture_director']['same_stage_delta_range']==[-1,0]
assert r['sequence_clocks']['capture_director']['sampled_transitions']==1
assert r['sequence_clocks']['capture_director']['sampled_objects']==2
assert len(r['guard_failures'])==1
old=summarize(Path(r'C:/Users/brett/Documents/Codex/work/arceus-runs/timing-20261106-125820-850/game-err.txt'))
assert old['engine_clocks']['script_frames']['sampled_calls']==0
assert old['physics_step']['step_seconds_range'] is None
assert old['physics_solver']['stored_seconds_range'] is None
assert old['physics_simulation']['normal_world_paths']==[]
assert old['ui_animation_clock']['sampled_calls']==0
assert old['sequence_clocks']['common_fade']['same_stage_delta_range'] is None
assert 119<old['physics_navigation']['rigid_body']['wrapper_calls_per_second_min_median_max'][1]<121
print('Research summary: fixture, non-finite/zero time, missing probes, and old-log compatibility passed.')
