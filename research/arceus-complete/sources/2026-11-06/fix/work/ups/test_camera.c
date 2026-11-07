#include "../taa/temporal_camera.h"
__declspec(dllexport) int camera_history(const float *cur,const float *prev,float *out) {
    return tmatrix(cur,prev,out);
}
