extern unsigned return_word(unsigned);
extern unsigned long long return_pair(unsigned long long);
extern float return_float(float);
extern double return_double(double);
unsigned call_word(unsigned x) { return return_word(x) + x; }
unsigned long long call_pair(unsigned long long x) { return return_pair(x) + x; }
float call_float(float x) { return return_float(x) + x; }
double call_double(double x) { return return_double(x) + x; }
