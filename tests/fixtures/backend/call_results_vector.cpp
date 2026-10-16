typedef __vector float V4;
extern V4 return_vector(V4);
V4 call_vector(V4 x) { return return_vector(x); }
