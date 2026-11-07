from pathlib import Path
p=Path(__file__).resolve().parent
f=p/'common.glsl';s=f.read_text().replace('uint historyValid;', 'uint historyValid;\n    float refineStep;').replace('statCount = 16','statCount = 25');f.write_text(s)
f=p/'material_flow.h';s=f.read_text().replace('FlowPropose, FlowVote','FlowPropose, FlowScore, FlowChoose, FlowVote').replace('uint32_t historyValid;','uint32_t historyValid;\n    float refineStep;').replace('w=16*g->bucketCount;', 'w=25*g->bucketCount;').replace('candidates_spv,validate_spv','candidates_spv,score_spv,choose_spv,validate_spv').replace('sizeof candidates_spv,sizeof validate_spv','sizeof candidates_spv,sizeof score_spv,sizeof choose_spv,sizeof validate_spv')
s=s.replace('    for(int i=0;i<FlowPassCount;++i) {\n        uint32_t groupX=', '''    const int passes[]={FlowMeasure,FlowPropose,FlowScore,FlowChoose,FlowScore,FlowChoose,FlowVote,FlowApply};
    for(unsigned pass=0;pass<sizeof passes/sizeof passes[0];++pass) {
        int i=passes[pass];
        if(i==FlowScore){
            params.refineStep=pass==2?3.0f:1.0f;
            vkCmdPushConstants(cmd,g->pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof params,&params);
        }
        uint32_t groupX=''').replace('if (i==FlowPropose) {groupX=16;','if (i==FlowPropose || i==FlowChoose) {groupX=16;')
f.write_text(s)
f=p/'build_shaders.py';s=f.read_text().replace("'candidates','validate'","'candidates','score','choose','validate'");f.write_text(s)
