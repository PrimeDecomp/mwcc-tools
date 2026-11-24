// flags: -O4,p -ipa off

extern void call(int);

struct Data {
    int value;
};

#pragma opt_optimizecontrolflow on

void consecutive(struct Data *data) {
    if (data->value != 1 && data->value != 2 && data->value != 3)
        return;
    call(255);
}

void negated_disjunction(struct Data *data) {
    if (!(data->value == 1 || data->value == 2 || data->value == 3))
        return;
    call(255);
}

void gap(struct Data *data) {
    if (data->value != 1 && data->value != 2 && data->value != 4)
        return;
    call(255);
}

void volatile_field(volatile struct Data *data) {
    if (data->value != 1 && data->value != 2 && data->value != 3)
        return;
    call(255);
}

#pragma opt_optimizecontrolflow off

void control_flow_disabled(struct Data *data) {
    if (data->value != 1 && data->value != 2 && data->value != 3)
        return;
    call(255);
}
