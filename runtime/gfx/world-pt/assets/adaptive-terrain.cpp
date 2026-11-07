// Offline bisection of a power-of-two height grid. Both triangles adjoining a
// split edge consult the same midpoint error, including their descendants.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstdint>
using std::vector;
struct Point { int x,z; };
static int side;
static bool fitColors;
static vector<float> height,error,predicted,colorError;
static vector<uint32_t> color, remap, vertices, indices;
static int index_of(Point p){return p.z*side+p.x;}
static Point middle(Point a,Point b){return {(a.x+b.x)/2,(a.z+b.z)/2};}
static bool leaf(Point a,Point b){return abs(a.x-b.x)+abs(a.z-b.z)<=2 && std::max(abs(a.x-b.x),abs(a.z-b.z))==1;}
static float color_error(uint32_t mid,uint32_t a,uint32_t b){
    float maximum=0;
    for(int shift=0;shift<32;shift+=8)
        maximum=std::max(maximum,float(abs(2*int((mid>>shift)&255)-int((a>>shift)&255)-int((b>>shift)&255)))/510.f);
    return maximum;
}
static void propagate(Point a,Point b,Point c,int remaining){
    if(leaf(a,b))return;
    Point m=middle(a,b);
    if(remaining){propagate(c,a,m,remaining-1);propagate(b,c,m,remaining-1);return;}
    int i=index_of(m),j=index_of(a),k=index_of(b);
    float local=fabsf(height[i]-.5f*(height[j]+height[k]));
    // Include the material footprint so a flat meadow still resolves its paths.
    if(fitColors)local=std::max(local,color_error(color[i],color[j],color[k]));
    float children=0;
    if(!leaf(c,a))children=std::max(error[index_of(middle(c,a))],error[index_of(middle(b,c))]);
    error[i]=std::max(error[i],local+children);
}
static unsigned mesh(Point a,Point b,Point c,float tolerance,bool emit){
    Point m=middle(a,b);
    if(!leaf(a,b) && error[index_of(m)]>tolerance)
        return mesh(c,a,m,tolerance,emit)+mesh(b,c,m,tolerance,emit);
    if(emit){
        Point face[]={a,b,c};
        int area=(b.x-a.x)*(c.z-a.z)-(b.z-a.z)*(c.x-a.x);
        if(area>0)std::swap(face[1],face[2]);
        for(Point p:face){
            int i=index_of(p);
            if(remap[i]==UINT32_MAX){remap[i]=uint32_t(vertices.size());vertices.push_back(i);}
            indices.push_back(remap[i]);
        }
        // Check the interpolant against every native grid vertex in this face.
        for(int z=std::min({a.z,b.z,c.z});z<=std::max({a.z,b.z,c.z});z++)
            for(int x=std::min({a.x,b.x,c.x});x<=std::max({a.x,b.x,c.x});x++){
                int u=(x-a.x)*(c.z-a.z)-(z-a.z)*(c.x-a.x);
                int v=(b.x-a.x)*(z-a.z)-(b.z-a.z)*(x-a.x);
                float s=float(u)/area,t=float(v)/area;
                if(s>=0 && t>=0 && s+t<=1)
                {
                    int pixel=z*side+x;
                    predicted[pixel]=(1-s-t)*height[index_of(a)]+s*height[index_of(b)]+t*height[index_of(c)];
                    float delta=0;
                    for(int shift=0;shift<32;shift+=8){
                        float va=(color[index_of(a)]>>shift)&255,vb=(color[index_of(b)]>>shift)&255,vc=(color[index_of(c)]>>shift)&255;
                        float actual=(color[pixel]>>shift)&255;
                        delta=std::max(delta,fabsf(actual-((1-s-t)*va+s*vb+t*vc)));
                    }
                    colorError[pixel]=delta;
                }
            }
    }
    return 1;
}
int main(int argc,char **argv){
    if(argc!=5)return 1;
    fitColors=atoi(argv[4])!=0;
    side=atoi(argv[1]);int span=side-1;
    if(span<2 || span>4096 || (span&(span-1)))return 2;
    size_t n=size_t(side)*side;
    FILE *in=fopen(argv[2],"rb");if(!in)return 3;
    height.resize(n);color.resize(n);
    if(fread(height.data(),4,n,in)!=n || fread(color.data(),4,n,in)!=n || fgetc(in)!=EOF)return 4;
    fclose(in);
    error.resize(n);predicted.assign(n,NAN);colorError.assign(n,NAN);remap.assign(n,UINT32_MAX);
    Point a={span,0},b={0,span},c={0,0},d={span,span};
    for(int level=2*int(log2(span))-1;level>=0;level--){
        propagate(a,b,c,level);propagate(b,a,d,level);
    }
    auto count=[&](float t){return mesh(a,b,c,t,false)+mesh(b,a,d,t,false);};
    float low=0,high=.02f;
    if(count(high)>524288){
        while(count(high)>524288)high*=2;
        low=high/2;
        for(int i=0;i<15;i++){float m=(low+high)*.5f;if(count(m)>524288)low=m;else high=m;}
    }
    mesh(a,b,c,high,true);mesh(b,a,d,high,true);
    FILE *out=fopen(argv[3],"wb");if(!out)return 5;
    uint32_t head[]={uint32_t(vertices.size()),uint32_t(indices.size()/3)};
    bool ok=fwrite(head,4,2,out)==2 && fwrite(vertices.data(),4,vertices.size(),out)==vertices.size()
        && fwrite(indices.data(),4,indices.size(),out)==indices.size()
        && fwrite(predicted.data(),4,n,out)==n && fwrite(colorError.data(),4,n,out)==n;
    if(fclose(out) || !ok)return 6;
    printf("%u vertices, %u triangles, split threshold %.6f\n",head[0],head[1],high);
}
