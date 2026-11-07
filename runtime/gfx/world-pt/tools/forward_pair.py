"""Run the original and linked native fragments in one invocation at selected QA presents."""
import re

def pair_entry(original,linked,ptr):
    entry=re.search(r'OpEntryPoint Fragment (%\w+)',original)[1]
    header=re.search(r'(?m)^\s*'+entry+r' = OpFunction %void None (%\w+)\s*$',original)
    func_type=header[1]
    body=original[header.start():]
    if body.count('OpFunctionEnd')!=1 or re.search(r'Op(?:Atomic|ImageWrite)|OpVariable .* (?:Private|StorageBuffer|Workgroup)\b',original):
        raise ValueError('Forward comparison requires a single fragment entry with no external writes')
    locals=set(re.findall(r'(?m)^\s*(%\w+) =',body))
    native=re.sub(r'%\w+',lambda m:'%native_'+m[0][1:] if m[0] in locals else m[0],body)
    native_entry='%native_'+entry[1:]
    annotations=[]
    for match in re.finditer(r'(?m)^\s*OpDecorate (%\w+) (.+)$',original):
        if match[1] in locals:annotations.append('OpDecorate %native_'+match[1][1:]+' '+match[2])
    annotations.extend(['OpDecorate %pairEnabled LinkageAttributes "pair_enabled" Import',
                        'OpDecorate %pairRecord LinkageAttributes "pair_record" Import'])
    pos=linked.index('OpDecorate ')
    linked=linked[:pos]+'\n'.join(annotations)+'\n'+linked[pos:]
    out=next(k for k in re.findall(r'OpDecorate (%\w+) Location 0\b',original)
             if re.search(re.escape(k)+r' = OpVariable %\w+ Output',original))
    probe_type=re.search(r'(%\w+) = OpTypeFunction %bool\s*$',linked,re.M)
    ty=probe_type[1] if probe_type else '%pairEnabledType'
    declarations=('' if probe_type else ty+' = OpTypeFunction %bool\n')
    declarations+=f"""%pairRecordType = OpTypeFunction %void {ptr} {ptr} {ptr}
%pairEnabled = OpFunction %bool None {ty}
OpFunctionEnd
%pairRecord = OpFunction %void None %pairRecordType
%pairBeforeImport = OpFunctionParameter {ptr}
%pairAfterImport = OpFunctionParameter {ptr}
%pairScreenImport = OpFunctionParameter {ptr}
OpFunctionEnd
"""
    pos=re.search(r'(?m)^\s*%\w+ = OpFunction ',linked).start()
    linked=linked[:pos]+'\n'+declarations+linked[pos:]
    linked=linked.replace('OpEntryPoint Fragment '+entry+' ', 'OpEntryPoint Fragment %pairMain ')
    linked=linked.replace('OpExecutionMode '+entry+' ','OpExecutionMode %pairMain ')
    return linked+'\n'+native+f"""
%pairMain = OpFunction %void None {func_type}
%pairStart = OpLabel
%pairBefore = OpVariable {ptr} Function
%pairAfter = OpVariable {ptr} Function
%pairScreen = OpVariable {ptr} Function
%pairTest = OpFunctionCall %bool %pairEnabled
OpSelectionMerge %pairContinue None
OpBranchConditional %pairTest %pairBaseline %pairContinue
%pairBaseline = OpLabel
%callNative = OpFunctionCall %void {native_entry}
%baseColor = OpLoad %v4float {out}
OpStore %pairBefore %baseColor
OpBranch %pairContinue
%pairContinue = OpLabel
%callTraced = OpFunctionCall %void {entry}
OpSelectionMerge %pairEnd None
OpBranchConditional %pairTest %pairReport %pairEnd
%pairReport = OpLabel
%tracedColor = OpLoad %v4float {out}
%pairCoord = OpLoad %v4float %gl_FragCoord
OpStore %pairAfter %tracedColor
OpStore %pairScreen %pairCoord
%report = OpFunctionCall %void %pairRecord %pairBefore %pairAfter %pairScreen
OpBranch %pairEnd
%pairEnd = OpLabel
OpReturn
OpFunctionEnd
"""
