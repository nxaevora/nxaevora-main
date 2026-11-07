#ifndef ARCEUS_TEMPORAL_CAMERA_H
#define ARCEUS_TEMPORAL_CAMERA_H
#include <math.h>

/* Row-vector inverse view-projections come from the guest. The resulting
   row-major product can be uploaded directly to GLSL's column-major mat4. */
static int tmatrix(const float *current, const float *previous, float *result) {
    double inverse[4][8];
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            inverse[row][col] = previous[row * 4 + col];
            inverse[row][col + 4] = (row == col);
        }
    }
    for (int col = 0; col < 4; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 4; ++row)
            if (fabs(inverse[row][col]) > fabs(inverse[pivot][col])) pivot = row;
        if (!(fabs(inverse[pivot][col]) > 1e-10)) return 0;
        for (int k = 0; k < 8; ++k) {
            double tmp = inverse[col][k];
            inverse[col][k] = inverse[pivot][k];
            inverse[pivot][k] = tmp;
        }
        double scale = inverse[col][col];
        for (int k = 0; k < 8; ++k) inverse[col][k] /= scale;
        for (int row = 0; row < 4; ++row) {
            if (row == col) continue;
            double factor = inverse[row][col];
            for (int k = 0; k < 8; ++k) inverse[row][k] -= factor * inverse[col][k];
        }
    }
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            double value = 0;
            for (int k = 0; k < 4; ++k) value += current[row * 4 + k] * inverse[k][4 + col];
            result[row * 4 + col] = (float)value;
            if (!isfinite(result[row * 4 + col])) return 0;
        }
    }

    /* Test projected positions at two representative reverse-Z depths.
       A camera translation can have a large coefficient yet only move visible
       pixels a few percent of the frame. Reject broad jumps at either depth. */
    const float depths[] = {0.01f, 0.05f};
    for (int plane = 0; plane < 2; ++plane) {
        int nearby = 0;
        for (int row = -1; row <= 1; ++row) {
            for (int col = -1; col <= 1; ++col) {
                float x = 0.6f * col, y = 0.6f * row, z = depths[plane];
                float q[4];
                for (int k = 0; k < 4; ++k)
                    q[k] = x * result[k] + y * result[4 + k] + z * result[8 + k] + result[12 + k];
                if (q[3] > 0 && q[2] >= 0 && q[2] <= q[3] &&
                    fabsf(q[0] / q[3] - x) < 0.5f && fabsf(q[1] / q[3] - y) < 0.5f) ++nearby;
            }
        }
        if (nearby < 5) return 0;
    }
    return 1;
}
#endif
