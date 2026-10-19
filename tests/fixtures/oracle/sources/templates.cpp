extern int consume(int);
template<class T> inline T adjust(T x) { return (x+3)*(x-2); }
int (*indirect)(int)=adjust<int>;

struct Value {
    int x;
    int y;
    Value(int a, int b):x(a),y(b) {}
    ~Value() { if(x<0) consume(y); }
};

inline int combine(Value v) { return adjust(v.x)+v.y; }

int with_value(int a,int b) {
    Value v(a,b);
    return combine(v)+indirect(a);
}
