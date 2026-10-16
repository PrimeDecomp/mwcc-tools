extern unsigned observe_word(unsigned value);
extern unsigned long long observe_pair(unsigned long long value);

unsigned long long object_bindings(unsigned long long input, unsigned control) {
    unsigned flag = observe_word(control);
    unsigned long long live = input + control;
    unsigned long long result = observe_pair(live);
    if (flag) {
        result += live;
    }
    return result + input;
}
