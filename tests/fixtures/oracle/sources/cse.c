extern int consume(int);

int common_integer(int a, int b, int c) {
    int x = (a+b)*(a-b);
    int y = (a+b)*(a-b)+c;
    return consume(x)+y+(a+b)*(a-b);
}

float common_float(const float *values, int index) {
    float x = values[index]*values[index+1];
    return x+x+values[index]*values[index+1];
}

int common_branch(int a, int b, int condition) {
    int x = (a+b)*(a-b);
    if (condition) x += (a+b)*(a-b);
    else x -= (a+b)*(a-b);
    return x;
}
