import sys
sys.path.pop(0)
from pathlib import Path
root=Path('work/pass10-render')
s=Path(r'C:/Users/brett/Documents/Codex/work/nvn-src/tests/shader_screen_fetch.c').read_text()
addition=r'''
static u64 twice(int dest, int source) {
    return 0x3868104000070000ull | (u64)source << 8 | dest;
}
static void twice_tests(const char* out) {
    u64 base[] = {ipa(0, 2), ipa(1, 3), twice(4,2), twice(5,3), f2i(2,4), f2i(3,5), tld(2)};
    u64 v[32];
    test(out, "twice-tld", base, 7, 1);
    memcpy(v,base,sizeof base);v[6]=tlds(2,2,3);
    test(out, "twice-tlds", v, 7, 1);
    memcpy(v,base,sizeof base);v[4]=mov(8,4);v[5]=mov(9,5);v[6]=f2i(2,8);v[7]=f2i(3,9);v[8]=tld(2);
    test(out, "twice-float-moves", v, 9, 1);
    memcpy(v,base,sizeof base);v[6]=mov(8,2);v[7]=mov(9,3);v[8]=tld(8);
    test(out, "twice-integer-moves", v, 9, 1);
    memcpy(v,base,sizeof base);v[2]=field(v[2],20,19,0x3fc00);v[3]=field(v[3],20,19,0x3fc00);
    test(out, "twice-reject-1p5", v, 7, 0);
    memcpy(v,base,sizeof base);v[2]=field(v[2],48,1,1);v[3]=field(v[3],48,1,1);
    test(out, "twice-reject-negate", v, 7, 0);
    memcpy(v,base,sizeof base);v[2]=field(v[2],50,1,1);v[3]=field(v[3],50,1,1);
    test(out, "twice-reject-saturate", v, 7, 0);
    memcpy(v,base,sizeof base);v[2]=field(v[2],44,2,2);v[3]=field(v[3],44,2,2);
    test(out, "twice-reject-fmz", v, 7, 0);
    memcpy(v,base,sizeof base);v[2]=field(v[2],41,3,1);v[3]=field(v[3],41,3,1);
    test(out, "twice-reject-postscale", v, 7, 0);
    memcpy(v,base,sizeof base);v[2]=field(v[2],16,4,0);v[3]=field(v[3],16,4,0);
    test(out, "twice-reject-predicated", v, 7, 0);
    memcpy(v,base,sizeof base);v[3]=mov(5,3);
    test(out, "twice-reject-mixed-axes", v, 7, 0);
    memcpy(v,base,sizeof base);v[4]=twice(4,4);v[5]=twice(5,5);v[6]=f2i(2,4);v[7]=f2i(3,5);v[8]=tld(2);
    test(out, "twice-reject-repeat", v, 9, 0);
    memcpy(v,base,sizeof base);v[4]=field(mov(4,255),16,4,0);v[5]=f2i(2,4);v[6]=f2i(3,5);v[7]=tld(2);
    test(out, "twice-reject-ambiguous-write", v, 8, 0);
    memcpy(v,base,sizeof base);v[6]=field(tld(2)|(1ull<<35),20,8,6);
    test(out, "twice-reject-fetch-offset", v, 7, 0);
}
'''
s=s.replace('int main(int argc, char** argv) {',addition+'\nint main(int argc, char** argv) {')
s=s.replace('    flow_tests(out);','    flow_tests(out);\n    twice_tests(out);')
(root/'shader_screen_fetch.c').write_text(s)
