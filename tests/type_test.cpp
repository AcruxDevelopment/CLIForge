#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include <cliforge/cliforge.hpp>

using cliforge::Engine;

void allTypes(int8_t a, uint8_t b, int16_t c, uint16_t d, int32_t e, uint32_t f, int64_t g,
              uint64_t h, float fl, double db, const std::string& s, char ch, bool bl) {
    std::printf(
        "i8=%d u8=%u i16=%d u16=%u i32=%d u32=%u i64=%lld u64=%llu f=%g d=%g s=%s c=%c b=%d\n",
        a, b, c, d, e, f, static_cast<long long>(g), static_cast<unsigned long long>(h),
        static_cast<double>(fl), db, s.c_str(), ch, bl);
}

int main(int argc, char** argv) {
    Engine cli("typetest");
    cli.command()
        .keyword("all")
        .parameter<int8_t>("a", "int8")
        .parameter<uint8_t>("b", "uint8")
        .parameter<int16_t>("c", "int16")
        .parameter<uint16_t>("d", "uint16")
        .parameter<int32_t>("e", "int32")
        .parameter<uint32_t>("f", "uint32")
        .parameter<int64_t>("g", "int64")
        .parameter<uint64_t>("h", "uint64")
        .parameter<float>("fl", "float32")
        .parameter<double>("db", "float64")
        .parameter<std::string>("s", "string")
        .parameter<char>("ch", "char")
        .flag("bl", 'b', "bool flag")
        .describe("Exercises every scalar type")
        .action(&allTypes);

    return cli.run(argc, argv);
}
