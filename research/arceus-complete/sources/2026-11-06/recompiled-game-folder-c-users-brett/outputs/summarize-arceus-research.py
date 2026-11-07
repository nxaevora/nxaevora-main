"""Summarize local camera/research logs; sampled rows are not display-FPS evidence."""
from pathlib import Path
import json,math,re,sys,statistics

def summarize(path):
    rows={}; guards=[]
    for line in path.read_text(errors='replace').splitlines():
        if not line.startswith('arceus-'): continue
        if 'refused' in line or 'compile failed' in line: guards.append(line)
        tag,_,body=line.partition(': ')
        values=dict(re.findall(r'([a-z_][a-z_0-9]*)=([^\s]+)',body))
        if 'count' in values: rows.setdefault(tag,[]).append(values)
    result={'source':str(path),'guard_failures':guards,'sampling_note':'Counts are cumulative; intervals can include pauses, menus, and inactive camera modes. No displayed-FPS claim.'}
    rates=rows.get('arceus-camera-rate',[])
    result['camera']={'observed_settings':sorted({float(r['authored']) for r in rates}), 'sampled_calls':len(rates),'highest_call_count':max((int(r['count']) for r in rates),default=0),'speed_multipliers':sorted({float(r['speed']) for r in rates if 'speed' in r})}
    inputs=rows.get('arceus-camera-input',[])
    result['camera']['input_callers']=sorted({r['caller'] for r in inputs if 'caller' in r})
    result['physics_navigation']={}
    for kind,name in [('0','navigation'),('1','rigid_body')]:
        samples=[r for r in rows.get('arceus-timing-research',[]) if r.get('kind')==kind]
        result['physics_navigation'][name]={'sampled_calls':len(samples),'mode_flags':sorted({int(r['variable_mode']) for r in samples}), 'incoming_dt_range':([min(float(r['incoming_dt']) for r in samples),max(float(r['incoming_dt']) for r in samples)] if samples else None)}
        objects={r.get('object') for r in samples}
        intervals=[]
        if len(objects)==1:
            stable=[r for r in samples if int(r['count'])>=300 and 'ns' in r]
            for a,b in zip(stable,stable[1:]):
                dc=int(b['count'])-int(a['count']);dn=int(b['ns'])-int(a['ns'])
                if dc>0 and dn>0: intervals.append(dc*1e9/dn)
        result['physics_navigation'][name]['wrapper_calls_per_second_min_median_max']=[min(intervals),statistics.median(intervals),max(intervals)] if intervals else None
        result['physics_navigation'][name]['cadence_note']='Wrapper-entry cadence, not displayed FPS or solver execution. Suppressed for multiple sampled objects.'
    result['engine_clocks']={}
    physics=rows.get('arceus-physics-step',[])
    dt=[float(r['step_seconds']) for r in physics if math.isfinite(float(r['step_seconds']))]
    result['physics_step']={'sampled_calls':len(physics),
        'highest_call_count':max((int(r['count']) for r in physics),default=0),
        'step_seconds_range':[min(dt),max(dt)] if dt else None,
        'callers':sorted({r['caller'] for r in physics if 'caller' in r}),
        'note':'StepInfo passed to Havok collision task setup. Does not alone prove how every body is integrated.'}
    solvers=rows.get('arceus-physics-solver',[])
    normal=[r for r in solvers if r.get('alternate_world')=='0']
    def finite_range(samples,key):
        values=[float(r[key]) for r in samples if key in r and math.isfinite(float(r[key]))]
        return [min(values),max(values)] if values else None
    result['physics_solver']={'sampled_calls':len(solvers),'normal_world_samples':len(normal),
        'alternate_world_samples':sum(r.get('alternate_world')=='1' for r in solvers),
        'stored_seconds_range':finite_range(normal,'stored_seconds'),
        'substep_seconds_range':finite_range(normal,'substep_seconds'),
        'substeps':sorted({int(r['substeps']) for r in normal}),
        'input_storage_mismatches':sum(not math.isclose(float(r['input_seconds']),float(r['stored_seconds']),rel_tol=2e-5,abs_tol=1e-8) for r in normal),
        'note':'Post-call world fields; alternate-world samples excluded from comparisons. Not proof of body trajectories.'}
    simulations=rows.get('arceus-physics-simulation',[])
    primary=[r for r in simulations if r.get('alternate_world')=='0']
    result['physics_simulation']={'sampled_calls':len(simulations),
        'normal_world_paths':sorted({r['path'] for r in primary}),
        'normal_world_samples_by_path':{path:sum(r['path']==path for r in primary) for path in sorted({r['path'] for r in primary})},
        'normal_world_tags':sorted({int(r['tag']) for r in primary}),
        'alternate_world_samples':len(simulations)-len(primary),
        'note':'Implementation identity observed after collision setup. Single-threaded solving can occur inline before task submission; this does not count completed solves.'}
    # A run can contain multiple worlds with different step policies. Join only
    # exact sampled invocations, never adjacent log lines or global count deltas.
    def invocation(row):
        return tuple(row[k] for k in ('count','ns','object')) if all(k in row for k in ('count','ns','object')) else None
    def invocation_index(samples):
        index={}
        for row in samples:
            key=invocation(row)
            if key is not None: index.setdefault(key,[]).append(row)
        return index
    solver_index=invocation_index(solvers)
    simulation_index=invocation_index(simulations)
    world_groups={}
    for row in physics:
        world_groups.setdefault((row.get('object','unknown'),row.get('caller','unknown')),[]).append(row)
    result['physics_worlds']=[]
    for (world,caller),samples in sorted(world_groups.items()):
        matched_solver=[];matched_simulation=[];ambiguous_solver=0;ambiguous_simulation=0;serial_observations=[]
        for row in samples:
            candidates=solver_index.get(invocation(row),[])
            if len(candidates)==1: matched_solver.append(candidates[0])
            elif len(candidates)>1: ambiguous_solver+=1
            candidates=simulation_index.get(invocation(row),[])
            if len(candidates)==1: matched_simulation.append(candidates[0])
            elif len(candidates)>1: ambiguous_simulation+=1
            solver_rows=solver_index.get(invocation(row),[])
            if len(solver_rows)==1 and len(candidates)==1:
                solver,simulation=solver_rows[0],candidates[0]
                if solver.get('alternate_world')=='0' and simulation.get('alternate_world')=='0' and 'postsolve_serial' in solver:
                    serial_observations.append({'count':int(row['count']),'ns':int(row['ns']),
                        'simulation':simulation['simulation'],'serial_u32':int(solver['postsolve_serial'])})
        ordinary=[r for r in matched_solver if r.get('alternate_world')=='0']
        ordinary_sim=[r for r in matched_simulation if r.get('alternate_world')=='0']
        result['physics_worlds'].append({
            'world':world,'caller':caller,'step_samples':len(samples),
            'step_seconds_range':finite_range(samples,'step_seconds'),
            'matched_solver_samples':len(matched_solver),
            'normal_stored_seconds_range':finite_range(ordinary,'stored_seconds'),
            'normal_substeps':sorted({int(r['substeps']) for r in ordinary}),
            'matched_simulation_samples':len(matched_simulation),
            'normal_paths':sorted({r['path'] for r in ordinary_sim}),
            'normal_simulation_objects':sorted({r['simulation'] for r in ordinary_sim}),
            'alternate_solver_samples':sum(r.get('alternate_world')=='1' for r in matched_solver),
            'ambiguous_solver_matches':ambiguous_solver,
            'ambiguous_simulation_matches':ambiguous_simulation,
            'postsolve_observations':sorted(serial_observations,key=lambda row:(row['ns'],row['count'])),
            'postsolve_note':'Raw exact-joined observations only. Serial is uint32 and wraps; a world/simulation address pair can be reused. No automatic rate or lifetime inference.',
            'note':'Exact count/ns/world joins. Addresses may be reused; these groups are not lifetime identities. Global sample counts do not establish per-world solve cadence.'})
    for kind,name in [('0','script_frames'),('1','ride_blink'),('2','ride_blink_subobject')]:
        samples=[r for r in rows.get('arceus-engine-clock',[]) if r.get('kind')==kind]
        item={'sampled_calls':len(samples), 'highest_call_count':max((int(r['count']) for r in samples),default=0),
              'callers':sorted({r['caller'] for r in samples if 'caller' in r}),
              'sampled_objects':len({r['object'] for r in samples if 'object' in r})}
        if kind=='0':
            ratios=[int(r['after'])/float(r['seconds']) for r in samples
                    if float(r.get('seconds','0'))>0 and math.isfinite(float(r['seconds']))]
            item['frames_per_elapsed_second_range']=[min(ratios),max(ratios)] if ratios else None
            item['per_caller']={}
            for caller in item['callers']:
                same=[r for r in samples if r.get('caller')==caller]
                valid=[int(r['after'])/float(r['seconds']) for r in same
                       if float(r.get('seconds','0'))>0 and math.isfinite(float(r['seconds']))]
                item['per_caller'][caller]={'samples':len(same),
                    'frames_per_elapsed_second_range':[min(valid),max(valid)] if valid else None}
            item['note']='Paired accumulated context frames/seconds; not displayed FPS. Repeated reads are not summed.'
        result['engine_clocks'][name]=item
    result['frame_timers']={}
    for kind,name in [('0','camera_recenter'),('1','battle_idle'),('2','throwable_state_1'),('3','throwable_state_2'),('4','throwable_state_3')]:
        samples=[r for r in rows.get('arceus-frame-timer',[]) if r.get('kind')==kind]
        deltas=[float(r['after'])-float(r['before']) for r in samples
                if math.isfinite(float(r['after'])) and math.isfinite(float(r['before']))]
        result['frame_timers'][name]={'sampled_changes':len(samples),
            'highest_call_count':max((int(r['count']) for r in samples),default=0),
            'highest_change_count':max((int(r['changed']) for r in samples),default=0),
            'states':sorted({int(r['state']) for r in samples}),
            'delta_range':[min(deltas),max(deltas)] if deltas else None,
            'callers':sorted({r['caller'] for r in samples if 'caller' in r}),
            'note':'Only changed fields sampled; resets may be included. Call counts may span inactive states and multiple objects.'}
    ui=rows.get('arceus-ui-clock',[])
    progress=[int(r['duration_ns'])/int(r['frame_ns']) for r in ui if int(r['frame_ns'])>0]
    result['ui_animation_clock']={'sampled_calls':len(ui),
        'modes':sorted({int(r['mode']) for r in ui}),
        'frame_nanoseconds':sorted({int(r['frame_ns']) for r in ui}),
        'normalized_progress_range':[min(progress),max(progress)] if progress else None,
        'unknown_denominator_samples':sum(int(r['frame_ns'])==0 for r in ui),
        'note':'Elapsed duration divided by the selected authored-frame duration, before per-animation speed. Multiple UI owners may share one clock.'}
    result['sequence_clocks']={}
    for kind,name in [('0','common_fade'),('1','capture_director')]:
        samples=[r for r in rows.get('arceus-sequence-clock',[]) if r.get('kind')==kind]
        stable=[r for r in samples if all(r[a]==r[b] for a,b in
            [('state_before','state_after'),('mode_before','mode_after'),('duration_before','duration_after')])]
        deltas=[int(r['after'])-int(r['before']) for r in stable]
        result['sequence_clocks'][name]={'sampled_calls':len(samples),
            'highest_call_count':max((int(r['count']) for r in samples),default=0),
            'sampled_objects':len({r['object'] for r in samples}),
            'states':sorted({int(r[k]) for r in samples for k in ['state_before','state_after']}),
            'modes':sorted({int(r[k]) for r in samples for k in ['mode_before','mode_after']}),
            'authored_durations':sorted({int(r[k]) for r in samples for k in ['duration_before','duration_after']}),
            'sampled_transitions':len(samples)-len(stable),
            'same_stage_delta_range':[min(deltas),max(deltas)] if deltas else None,
            'callers':sorted({r['caller'] for r in samples}),
            'note':'Before/after one callback; stage, mode and duration changes excluded from delta range. Counts include multiple objects and inactive calls; no rate inferred.'}
    return result

if __name__=='__main__':
    p=Path(sys.argv[1]);p=p/'game-err.txt' if p.is_dir() else p
    print(json.dumps(summarize(p),indent=2))
