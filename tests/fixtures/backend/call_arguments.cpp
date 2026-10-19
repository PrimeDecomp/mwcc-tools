struct Pass { unsigned data[4]; };
extern const Pass kPass;
extern const Pass& kReference;
extern void SetTevOp(unsigned stage,const Pass& pass);
extern unsigned nested(unsigned value);
void address_argument() { SetTevOp(0,kPass); }
void reference_argument() { SetTevOp(0,kReference); }
void nested_argument(unsigned value) { SetTevOp(nested(value),kPass); }
extern void many(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,double,long long);
void stack_argument(unsigned value) { many(value,2,3,4,5,6,7,8,9,2.5,(long long)value); }
