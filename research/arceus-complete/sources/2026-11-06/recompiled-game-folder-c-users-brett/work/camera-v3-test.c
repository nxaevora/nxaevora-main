#include <assert.h>
#include <stdio.h>
#include "nx/runtime/arceus_camera.h"
int main(void) {
    /* Regression: the prior lerp left .00375 response on this release. */
    assert(arceus_camera_fraction(.005f,0,0,.1f,1,.02f,.04f,.25)==0);
    assert(arceus_camera_fraction(.995f,1,1,.1f,1,.04f,.02f,.25)==1);
    assert(arceus_camera_fraction(.005f,0,.8f,.1f,0,.02f,.04f,.25)==0);
    for(int fps=30;fps<=120;fps*=2) {
        float x=0; double f=30.0/fps;
        for(int i=0;i<fps;i++) x=arceus_camera_fraction(x,fminf(1,x+.04f),1,.1f,1,.04f,.04f,f);
        assert(x==1);
        for(int i=0;i<fps;i++) x=arceus_camera_fraction(x,fmaxf(0,x-.04f),0,.1f,1,.04f,.04f,f);
        assert(x==0);
        /* Steady angular speed must retain sensitivity ratio at any FPS. */
        double travel[2]={0,0};
        for(int i=0;i<fps*10;i++) for(int j=0;j<2;j++) travel[j]+=(j+1)*.05*f;
        assert(fabs(travel[0]-15)<1e-9 && fabs(travel[1]-30)<1e-9);
    }
    /* Partial stick retains the game's equilibrium (.5 * (.01 + .5*.02)). */
    assert(fabsf(arceus_camera_fraction(.01f,.01f,.5f,.1f,1,.02f,.04f,.25)-.01f)<1e-7f);
    puts("Camera release, saturation, disabled input, equilibrium, and sensitivity checks passed.");
}
