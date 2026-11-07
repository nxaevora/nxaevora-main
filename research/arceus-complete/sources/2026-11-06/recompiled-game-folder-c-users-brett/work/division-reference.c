long long frame30(long long ns) { return ns / 33333334; }
long long frame60(long long ns) { return ns / 16666667; }
int floatframe(float sec) { return (int)(sec * 30.f); }
