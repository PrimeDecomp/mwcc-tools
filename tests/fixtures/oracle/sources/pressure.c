extern int external(int);

int pressure(const int *data) {
    int a0=data[0], a1=data[1], a2=data[2], a3=data[3];
    int a4=data[4], a5=data[5], a6=data[6], a7=data[7];
    int a8=data[8], a9=data[9], a10=data[10], a11=data[11];
    int a12=data[12], a13=data[13], a14=data[14], a15=data[15];
    int a16=data[16], a17=data[17], a18=data[18], a19=data[19];
    int a20=data[20], a21=data[21], a22=data[22], a23=data[23];
    int a24=data[24], a25=data[25], a26=data[26], a27=data[27];
    int a28=data[28], a29=data[29], a30=data[30], a31=data[31];
    int a32=data[32], a33=data[33], a34=data[34], a35=data[35];
    int call=external(data[36]);
    return call+a0+a1+a2+a3+a4+a5+a6+a7+a8+a9+a10+a11
        +a12+a13+a14+a15+a16+a17+a18+a19+a20+a21+a22+a23
        +a24+a25+a26+a27+a28+a29+a30+a31+a32+a33+a34+a35;
}
