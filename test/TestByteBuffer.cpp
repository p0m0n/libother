#include "ByteBuffer.h"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace other::net::sockets;

namespace
{
    int g_totalTests  = 0;
    int g_passedTests = 0;
    int g_failedTests = 0;

    // Вспомогательная функция вывода результата теста в формате "Название : [  OK  ]" / "[ FAIL ]".
    void ReportTest(const std::string& name, bool condition)
    {
        ++g_totalTests;
        if (condition)
            ++g_passedTests;
        else
            ++g_failedTests;

        std::cout << std::left << std::setw(58) << name
                  << ": " << (condition ? "[  OK  ]" : "[ FAIL ]") << std::endl;
    }

    // Проверяет, что переданный блок кода выбрасывает ожидаемый тип исключения.
    template <typename ExceptionT, typename FuncT>
    bool ExpectThrow(FuncT&& fn)
    {
        try
        {
            fn();
            return false;
        }
        catch (const ExceptionT&)
        {
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    // 1. Тест конструкторов, копирования и перемещения.
    void TestConstructorsAndOwnership()
    {
        std::cout << "\n=== 1. Конструкторы, копирование и перемещение ===\n";

        // Конструктор по умолчанию
        ByteBuffer defBuf;
        ReportTest("Default ctor (empty, capacity, LittleEndian)",
            defBuf.Empty() &&
            defBuf.Size() == 0 &&
            defBuf.Capacity() >= ByteBuffer::DefaultCapacity &&
            defBuf.ReadOffset() == 0 &&
            defBuf.WriteOffset() == 0 &&
            defBuf.Remaining() == 0 &&
            defBuf.GetByteOrder() == ByteOrder::LittleEndian);

        // Конструктор с емкостью и BigEndian
        ByteBuffer beBuf(128, ByteOrder::BigEndian);
        ReportTest("Capacity + BigEndian ctor",
            beBuf.Empty() &&
            beBuf.Capacity() >= 128 &&
            beBuf.GetByteOrder() == ByteOrder::BigEndian);

        // Конструктор из std::vector<uint8_t>
        std::vector<uint8_t> initVec = { 0x10, 0x20, 0x30, 0x40 };
        ByteBuffer vecBuf(std::move(initVec), ByteOrder::LittleEndian);
        ReportTest("Vector move ctor (size, cursors, data)",
            vecBuf.Size() == 4 &&
            vecBuf.ReadOffset() == 0 &&
            vecBuf.WriteOffset() == 4 &&
            vecBuf.Remaining() == 4 &&
            vecBuf[0] == 0x10 && vecBuf[3] == 0x40);

        // Конструктор из сырого указателя и длины
        const uint8_t rawArr[] = { 0xAA, 0xBB, 0xCC };
        ByteBuffer rawBuf(rawArr, sizeof(rawArr));
        ReportTest("Raw pointer + size ctor",
            rawBuf.Size() == 3 &&
            rawBuf[0] == 0xAA && rawBuf[1] == 0xBB && rawBuf[2] == 0xCC);

        // Конструктор с nullptr и ненулевым размером должен бросить std::invalid_argument
        ReportTest("Raw pointer ctor (nullptr, size>0 -> invalid_argument)",
            ExpectThrow<std::invalid_argument>([]() { ByteBuffer bad(nullptr, 5); }));

        // Копирующий конструктор и независимость копии
        ByteBuffer copyBuf(rawBuf);
        copyBuf[0] = 0x11;
        ReportTest("Copy ctor (deep copy independence)",
            copyBuf.Size() == 3 && copyBuf[0] == 0x11 && rawBuf[0] == 0xAA);

        // Копирующее присваивание
        ByteBuffer copyAssign;
        copyAssign = rawBuf;
        copyAssign[1] = 0x22;
        ReportTest("Copy assignment (deep copy independence)",
            copyAssign.Size() == 3 && copyAssign[1] == 0x22 && rawBuf[1] == 0xBB);

        // Перемещающий конструктор
        ByteBuffer moveBuf(std::move(copyBuf));
        ReportTest("Move ctor (target has data, source cleared)",
            moveBuf.Size() == 3 && moveBuf[0] == 0x11 && copyBuf.Empty() && copyBuf.WriteOffset() == 0);

        // Перемещающее присваивание
        ByteBuffer moveAssign;
        moveAssign = std::move(moveBuf);
        ReportTest("Move assignment (target has data, source cleared)",
            moveAssign.Size() == 3 && moveAssign[0] == 0x11 && moveBuf.Empty());
    }

    // 2. Тест управления памятью и курсорами.
    void TestMemoryAndCursors()
    {
        std::cout << "\n=== 2. Управление памятью и курсорами ===\n";

        ByteBuffer buf(16);
        buf.SetByteOrder(ByteOrder::BigEndian);
        ReportTest("SetByteOrder / GetByteOrder (BigEndian)", buf.GetByteOrder() == ByteOrder::BigEndian);

        buf.Reserve(512);
        ReportTest("Reserve(512)", buf.Capacity() >= 512 && buf.Size() == 0);

        buf.Resize(10);
        ReportTest("Resize(10) (size=10, writeOffset=10, zero-filled)",
            buf.Size() == 10 && buf.WriteOffset() == 10 && buf[0] == 0 && buf[9] == 0);

        buf.SetReadOffset(4);
        ReportTest("SetReadOffset(4) / ReadOffset / Remaining",
            buf.ReadOffset() == 4 && buf.Remaining() == 6);

        ReportTest("SetReadOffset(OOB -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { buf.SetReadOffset(11); }));

        buf.SetWriteOffset(2);
        ReportTest("SetWriteOffset(2) / WriteOffset", buf.WriteOffset() == 2);

        ReportTest("SetWriteOffset(OOB -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { buf.SetWriteOffset(11); }));

        buf.Rewind();
        ReportTest("Rewind() (readOffset=0, data preserved)",
            buf.ReadOffset() == 0 && buf.Size() == 10);

        buf.ShrinkToFit();
        ReportTest("ShrinkToFit() (size preserved)", buf.Size() == 10);

        // TakeData должен отдать вектор и очистить буфер
        std::vector<uint8_t> taken = buf.TakeData();
        ReportTest("TakeData() (returns vector, clears buffer)",
            taken.size() == 10 && buf.Empty() && buf.ReadOffset() == 0 && buf.WriteOffset() == 0);

        // Clear
        buf.Write<uint32_t>(0x12345678);
        buf.Clear();
        ReportTest("Clear() (empty, cursors reset)",
            buf.Empty() && buf.Size() == 0 && buf.ReadOffset() == 0 && buf.WriteOffset() == 0);
    }

    // 3. Тест записи/чтения скаляров, Peek, WriteAt и порядка байт (Endianness).
    void TestScalarsAndEndianness()
    {
        std::cout << "\n=== 3. Скаляры, Peek, WriteAt и порядок байт ===\n";

        // LittleEndian проверка раскладки байтов
        ByteBuffer le(64, ByteOrder::LittleEndian);
        le.Write<uint16_t>(0x1234);
        le.Write<uint32_t>(0xAABBCCDD);
        ReportTest("LittleEndian byte layout (0x1234, 0xAABBCCDD)",
            le[0] == 0x34 && le[1] == 0x12 &&
            le[2] == 0xDD && le[3] == 0xCC && le[4] == 0xBB && le[5] == 0xAA);

        // BigEndian проверка раскладки байтов
        ByteBuffer be(64, ByteOrder::BigEndian);
        be.Write<uint16_t>(0x1234);
        be.Write<uint32_t>(0xAABBCCDD);
        be.Write<uint64_t>(0x0102030405060708ULL);
        ReportTest("BigEndian byte layout (16/32/64-bit)",
            be[0] == 0x12 && be[1] == 0x34 &&
            be[2] == 0xAA && be[3] == 0xBB && be[4] == 0xCC && be[5] == 0xDD &&
            be[6] == 0x01 && be[13] == 0x08);

        // Peek без сдвига курсора
        uint16_t peek16 = be.Peek<uint16_t>(0);
        uint32_t peek32 = be.Peek<uint32_t>(2);
        ReportTest("Peek<T> (correct value, cursor unchanged)",
            peek16 == 0x1234 && peek32 == 0xAABBCCDD && be.ReadOffset() == 0);

        ReportTest("Peek<T> (OOB -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { (void)be.Peek<uint32_t>(12); }));

        // WriteAt без сдвига _writeCursor
        size_t wBefore = be.WriteOffset();
        be.WriteAt<uint16_t>(0, 0x5678);
        ReportTest("WriteAt<T> (overwrites in place, writeOffset unchanged)",
            be.Peek<uint16_t>(0) == 0x5678 && be.WriteOffset() == wBefore);

        ReportTest("WriteAt<T> (OOB -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { be.WriteAt<uint32_t>(12, 0x11111111); }));

        // Read<T> в BigEndian
        ReportTest("Read<T> (BigEndian round-trip)",
            be.Read<uint16_t>() == 0x5678 &&
            be.Read<uint32_t>() == 0xAABBCCDD &&
            be.Read<uint64_t>() == 0x0102030405060708ULL &&
            be.Remaining() == 0);

        // Чтение за пределами буфера
        ReportTest("Read<T> (past end -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { (void)be.Read<uint8_t>(); }));

        // WriteBool / ReadBool (включая ненулевой байт 0x7F -> true)
        ByteBuffer boolBuf;
        boolBuf.WriteBool(true);
        boolBuf.WriteBool(false);
        boolBuf.Write<uint8_t>(0x7F);
        ReportTest("WriteBool / ReadBool (true, false, 0x7F->true)",
            boolBuf.ReadBool() == true &&
            boolBuf.ReadBool() == false &&
            boolBuf.ReadBool() == true);

        // Float / Double / Native order
        ByteBuffer fltBuf(64, ByteOrder::Native);
        fltBuf.Write<float>(-12.5f);
        fltBuf.Write<double>(3.141592653589793);
        float fVal = fltBuf.Read<float>();
        double dVal = fltBuf.Read<double>();
        ReportTest("Write/Read float & double (Native)",
            std::fabs(fVal - (-12.5f)) < 1e-6f && std::fabs(dVal - 3.141592653589793) < 1e-12);
    }

    // 4. Тест сырых байтов, WriteBuffer, WriteBytesAt и Skip.
    void TestRawBytesAndSkip()
    {
        std::cout << "\n=== 4. Сырые байты, WriteBuffer, WriteBytesAt, Skip ===\n";

        ByteBuffer buf;
        const uint8_t src[] = { 1, 2, 3, 4, 5 };
        buf.WriteBytes(src, sizeof(src));
        buf.WriteBytes(nullptr, 0); // Безопасная пустая запись

        ReportTest("WriteBytes(nullptr, 4 -> invalid_argument)",
            ExpectThrow<std::invalid_argument>([&]() { buf.WriteBytes(nullptr, 4); }));

        // WriteBuffer
        ByteBuffer extra;
        extra.Write<uint8_t>(6);
        extra.Write<uint8_t>(7);
        buf.WriteBuffer(extra);
        ReportTest("WriteBuffer (appends other buffer)",
            buf.Size() == 7 && buf[5] == 6 && buf[6] == 7);

        // WriteBytesAt
        const uint8_t patch[] = { 99, 100 };
        buf.WriteBytesAt(1, patch, 2);
        ReportTest("WriteBytesAt (in-place patch)",
            buf[0] == 1 && buf[1] == 99 && buf[2] == 100 && buf[3] == 4);

        ReportTest("WriteBytesAt (OOB -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { buf.WriteBytesAt(6, patch, 2); }));

        // ReadBytes (в массив) + Skip + ReadBytes (возврат vector)
        uint8_t dst[2] = {};
        buf.ReadBytes(dst, 2);
        buf.Skip(2); // Пропускаем индексы 2 и 3
        std::vector<uint8_t> tail = buf.ReadBytes(3);
        ReportTest("ReadBytes(dst) + Skip + ReadBytes(count)",
            dst[0] == 1 && dst[1] == 99 &&
            tail.size() == 3 && tail[0] == 5 && tail[1] == 6 && tail[2] == 7 &&
            buf.Remaining() == 0);

        ReportTest("ReadBytes(nullptr, 1 -> invalid_argument)",
            ExpectThrow<std::invalid_argument>([&]() { buf.ReadBytes(nullptr, 1); }));

        ReportTest("Skip (OOB -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { buf.Skip(1); }));
    }

    // 5. Тест строковых операций и валидации UTF-8.
    void TestStringsAndUtf8()
    {
        std::cout << "\n=== 5. Строки (Raw, CString, LengthPrefixed) и UTF-8 ===\n";

        ByteBuffer buf;
        // Явные UTF-8 байты ("Привет! ASCII 123" и "Prefixed_тест"), чтобы тест не зависел от /utf-8 в MSVC (CP1251).
        const std::string utf8Sample = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82! ASCII 123";
        const std::string utf8Pref   = "Prefixed_\xD1\x82\xD0\xB5\xD1\x81\xD1\x82";
        buf.WriteString("RAW");
        buf.WriteCString(utf8Sample);
        buf.WriteLengthPrefixedString(utf8Pref);
        buf.WriteLengthPrefixedString("");

        std::string sRaw  = buf.ReadString(3, true);
        std::string sCStr = buf.ReadCString(true);
        std::string sPref = buf.ReadLengthPrefixedString(true);
        std::string sEmp  = buf.ReadLengthPrefixedString(true);

        ReportTest("Write/Read String, CString, LengthPrefixed (UTF-8)",
            sRaw == "RAW" && sCStr == utf8Sample && sPref == utf8Pref && sEmp.empty() &&
            buf.Remaining() == 0);

        // ReadCString без завершающего '\0' -> std::out_of_range
        ByteBuffer noNul;
        noNul.WriteString("no_terminator");
        ReportTest("ReadCString (missing NUL -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { (void)noNul.ReadCString(); }));

        // Невалидный UTF-8 при validateUtf8 = true -> std::invalid_argument
        ByteBuffer badUtf8;
        const uint8_t badBytes[] = { 0xFF, 0xFE, 0x00 };
        badUtf8.WriteBytes(badBytes, sizeof(badBytes));
        ReportTest("ReadCString(validateUtf8=true -> invalid_argument)",
            ExpectThrow<std::invalid_argument>([&]() { (void)badUtf8.ReadCString(true); }));

        // Тот же буфер с validateUtf8 = false должен прочитаться без ошибки
        badUtf8.Rewind();
        std::string rawBad = badUtf8.ReadCString(false);
        ReportTest("ReadCString(validateUtf8=false -> succeeds)", rawBad.size() == 2);

        // Overlong UTF-8 (0xC0 0x80) и суррогат UTF-16 (0xED 0xA0 0x80) должны отклоняться
        ByteBuffer overlongBuf;
        const uint8_t overlong[] = { 0xC0, 0x80 };
        overlongBuf.WriteBytes(overlong, sizeof(overlong));
        ReportTest("ReadString(overlong UTF-8 -> invalid_argument)",
            ExpectThrow<std::invalid_argument>([&]() { (void)overlongBuf.ReadString(2, true); }));

        ByteBuffer surrogateBuf;
        const uint8_t surrogate[] = { 0xED, 0xA0, 0x80 };
        surrogateBuf.WriteBytes(surrogate, sizeof(surrogate));
        ReportTest("ReadString(UTF-16 surrogate -> invalid_argument)",
            ExpectThrow<std::invalid_argument>([&]() { (void)surrogateBuf.ReadString(3, true); }));
    }

    // 6. Тест побитовой упаковки и распаковки.
    void TestBitPacking()
    {
        std::cout << "\n=== 6. Побитовая упаковка / распаковка ===\n";

        ByteBuffer buf;
        buf.WriteBit(true);
        buf.WriteBit(false);
        buf.WriteBit(true);
        buf.WriteBits(0b10110, 5); // Итого ровно 8 бит (1 байт: 10110110 = 0xB6)

        const uint64_t val64 = 0xCAFEBABE12345678ULL;
        buf.WriteBits(val64, 64);
        buf.WriteBits(0b1101, 4);
        buf.AlignWriteToByte(); // Дополняем оставшиеся 4 бита нулями
        buf.Write<uint16_t>(0xBEEF); // Смешивание битовых и обычных байтовых данных

        bool b0 = buf.ReadBit();
        bool b1 = buf.ReadBit();
        bool b2 = buf.ReadBit();
        uint64_t v5  = buf.ReadBits(5);
        uint64_t v64 = buf.ReadBits(64);
        uint64_t v4  = buf.ReadBits(4);
        buf.AlignReadToByte();
        uint16_t afterBits = buf.Read<uint16_t>();

        ReportTest("WriteBit/WriteBits/ReadBit/ReadBits (1..64 bits + align)",
            b0 == true && b1 == false && b2 == true &&
            v5 == 0b10110ULL && v64 == val64 && v4 == 0b1101ULL &&
            afterBits == 0xBEEF && buf.Remaining() == 0);

        ReportTest("WriteBits (bitCount=0 -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { buf.WriteBits(1, 0); }));

        ReportTest("WriteBits (bitCount=65 -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { buf.WriteBits(1, 65); }));

        ReportTest("ReadBits (bitCount=0 -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { (void)buf.ReadBits(0); }));

        ReportTest("ReadBits (bitCount=65 -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { (void)buf.ReadBits(65); }));

        ReportTest("ReadBit (past end -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { (void)buf.ReadBit(); }));
    }

    // 7. Тест всех потоковых операторов << и >>.
    void TestStreamOperators()
    {
        std::cout << "\n=== 7. Потоковые операторы << и >> ===\n";

        ByteBuffer buf(256, ByteOrder::BigEndian);
        const char* nullCStr = nullptr;

        buf << true << false << 'K'
            << uint8_t(0xFA) << uint16_t(0x1234) << uint32_t(0x89ABCDEF) << uint64_t(0x0123456789ABCDEFULL)
            << int8_t(-12) << int16_t(-1234) << int32_t(-123456) << int64_t(-9876543210LL)
            << 2.5f << -4.75
            << std::string_view("view_str")
            << std::string("std_str")
            << "c_literal"
            << nullCStr;

        bool b1 = false, b2 = true;
        char ch = 0;
        uint8_t  u8 = 0;  uint16_t u16 = 0; uint32_t u32 = 0; uint64_t u64 = 0;
        int8_t   i8 = 0;  int16_t  i16 = 0; int32_t  i32 = 0; int64_t  i64 = 0;
        float    f  = 0;  double   d   = 0;
        std::string s1, s2, s3, s4;

        buf >> b1 >> b2 >> ch
            >> u8 >> u16 >> u32 >> u64
            >> i8 >> i16 >> i32 >> i64
            >> f >> d
            >> s1 >> s2 >> s3 >> s4;

        ReportTest("All operator<< and operator>> overloads round-trip",
            b1 == true && b2 == false && ch == 'K' &&
            u8 == 0xFA && u16 == 0x1234 && u32 == 0x89ABCDEF && u64 == 0x0123456789ABCDEFULL &&
            i8 == -12 && i16 == -1234 && i32 == -123456 && i64 == -9876543210LL &&
            std::fabs(f - 2.5f) < 1e-6f && std::fabs(d - (-4.75)) < 1e-12 &&
            s1 == "view_str" && s2 == "std_str" && s3 == "c_literal" && s4.empty() &&
            buf.Remaining() == 0);
    }

    // 8. Тест индексации operator[] и диагностических методов.
    void TestIndexingAndDiagnostics()
    {
        std::cout << "\n=== 8. Индексация operator[] и диагностика ===\n";

        ByteBuffer emptyBuf;
        ReportTest("Diagnostics on empty buffer (Hex/Dec/Ascii empty)",
            emptyBuf.ToHexString().empty() &&
            emptyBuf.ToDecimalString().empty() &&
            emptyBuf.ToAsciiString().empty());

        ByteBuffer buf;
        const uint8_t sample[] = { 'A', 'B', 0x00, 0xFF };
        buf.WriteBytes(sample, sizeof(sample));

        // Проверка mutable и const operator[]
        buf[1] = 'Z';
        const ByteBuffer& cbuf = buf;
        ReportTest("operator[] mutable & const access",
            buf[0] == 'A' && cbuf[1] == 'Z' && cbuf[3] == 0xFF);

        ReportTest("operator[] (OOB -> out_of_range)",
            ExpectThrow<std::out_of_range>([&]() { (void)cbuf[4]; }));

        ReportTest("ToHexString() format check",
            buf.ToHexString() == "41 5A 00 FF");

        ReportTest("ToDecimalString() format check",
            buf.ToDecimalString() == "65 90 0 255");

        ReportTest("ToAsciiString() format check (non-printable -> '.')",
            buf.ToAsciiString() == "AZ..");
    }
}

int main()
{
	setlocale(LC_ALL, "Russian");
	
    std::cout << "====================================================================\n";
    std::cout << "                   ByteBuffer Global Test Suite                     \n";
    std::cout << "====================================================================\n";

    TestConstructorsAndOwnership();
    TestMemoryAndCursors();
    TestScalarsAndEndianness();
    TestRawBytesAndSkip();
    TestStringsAndUtf8();
    TestBitPacking();
    TestStreamOperators();
    TestIndexingAndDiagnostics();

    std::cout << "\n====================================================================\n";
    std::cout << "ИТОГО: " << g_passedTests << " / " << g_totalTests << " пройдено, "
              << g_failedTests << " провалено -> "
              << (g_failedTests == 0 ? "[  OK  ]" : "[ FAIL ]") << std::endl;
    std::cout << "====================================================================\n";

    return (g_failedTests == 0) ? 0 : 1;
}
