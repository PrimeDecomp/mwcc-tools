// Isolate the same symbolic const-reference argument path as EnvModulate.
struct Pass {
    unsigned words[4];
};
struct Graphics {
    static const Pass symbol;
    static const Pass &Get() { return symbol; }
    static void Set(unsigned stage, const Pass &pass);
};
void symbolic_address(unsigned enabled) {
    Graphics::Set(0, Graphics::Get());
    if (enabled)
        Graphics::Set(0, Graphics::Get());
}
