extern int external(int);

int across_calls(int a, int b, int c) {
    int x = external(a);
    int y = external(b);
    int z = external(c);
    return x + y + z + a + b + c;
}

float accumulate(const float *values, int count, float scale) {
    float sum = 0.0f;
    int i;
    for (i = 0; i < count; ++i) {
        sum += values[i] * scale;
    }
    return sum;
}

