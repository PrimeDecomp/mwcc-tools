extern int consume(int);
struct Pair { int first; int second; };

template <class T> inline T twice(T x) { return x + x; }
inline int nested(int a) { return twice(a) + 1; }

int pipeline(int input, int count) {
    Pair p = {input, nested(input)};
    int total = 0;
    for (int i = 0; i < count; ++i) {
        int shared = p.first * p.second;
        total += shared + (i ? shared : nested(i));
        if (total & 1) total = consume(total);
    }
    return total + p.second;
}
