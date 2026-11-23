#include "Base.h"

using namespace other::crypt;


struct BaseTest
{
    const std::string input;
    const std::string expected;
};

static BaseTest Base16Test[] =
{
    {"",             ""},
    {"f",            "66"},
    {"fo",           "666F"},
    {"foo",          "666F6F"},
    {"foob",         "666F6F62"},
    {"fooba",        "666F6F6261"},
    {"foobar",       "666F6F626172"}
};

static BaseTest Base26Test[] = 
{   
    {"",             ""},
    {"f",            "DY"},
    {"fo",           "BMUP"},
    {"foo",          "ORYTZ"},
    {"foob",         "FOQTWRY"},
    {"fooba",        "CCUFBRKQB"},
    {"foobar",       "UTIVWLKOCG"}
};

static BaseTest Base32Test[] =
{   
    {"",             ""},
    {"f",            "MY======"},
    {"fo",           "MZXQ===="},
    {"foo",          "MZXW6==="},
    {"foob",         "MZXW6YQ="},
    {"fooba",        "MZXW6YTB"},
    {"foobar",       "MZXW6YTBOI======"}
};

static BaseTest Base32HEXTest[] =
{  
    {"",             ""},
    {"f",            "CO======"},
    {"fo",           "CPNG===="},
    {"foo",          "CPNMU==="},
    {"foob",         "CPNMUOG="},
    {"fooba",        "CPNMUOJ1"},
    {"foobar",       "CPNMUOJ1E8======"}
};

static BaseTest Base32CrockfordTest[] =
{
    {"",               ""},
    {"f",              "CR======"},
    {"fo",             "CSQG===="},
    {"foo",            "CSQPY==="},
    {"foob",           "CSQPYRG="},
    {"fooba",          "CSQPYRK1"},
    {"foobar",         "CSQPYRK1E8======"}
};

static BaseTest Base36Test[] = 
{   
    {"",             ""},
    {"f",            "2U"},
    {"fo",           "K8F"},
    {"foo",          "3ZVXR"},
    {"foob",         "SF742Q"},
    {"fooba",        "5M42KZFL"},
    {"foobar",       "13X8YD7YWI"}
};

static BaseTest Base45Test[] =
{
    {"",             ""},
    {"f",            "C2"},
    {"fo",           "X.C"},
    {"foo",          "X.CL2"},
    {"foob",         "X.CT3E"},
    {"fooba",        "X.CT3E72"},
    {"foobar",       "X.CT3EGEC"}
};

static BaseTest Base58FlickrTest[] =
{   
    {"",             ""},
    {"f",            "2L"},
    {"fo",           "8N8"},
    {"foo",          "ApAP"},
    {"foob",         "3BSaF9"},
    {"fooba",        "cyiqGLZ"},
    {"foobar",       "T1yV2Yzy"}
};

static BaseTest Base58RippleTest[] =
{   
    {"",             ""},
    {"f",            "pm"},
    {"fo",           "3o3"},
    {"foo",          "bQbF"},
    {"foob",         "sc1wg9"},
    {"fooba",        "UZJR6mz"},
    {"foobar",       "trZvpy2Z"}
};

static BaseTest Base58BitcoinTest[] = 
{   
    {"",             ""},
    {"f",            "2m"},
    {"fo",           "8o8"},
    {"foo",          "bQbp"},
    {"foob",         "3csAg9"},
    {"fooba",        "CZJRhmz"},
    {"foobar",       "t1Zv2yaZ"},

    {"Hello World!", "2NEpo7TZRRrLZSi2U"},
    {"The quick brown fox jumps over the lazy dog.", "USm3fpXnKG5EUBx2ndxBDMPVciP5hGey2Jh4NDv6gmeo1LkMeiKrLJUUBk6Z"}
};

static BaseTest Base62Test[] = 
{   
    {"",             ""},
    {"f",            "1e"},
    {"fo",           "6ox"},
    {"foo",          "SAPP"},
    {"foob",         "1sIyuo"},
    {"fooba",        "7kENWa1"},
    {"foobar",       "VytN8Wjy"},
};

static BaseTest Base64Test[] =
{   
    {"",             ""},
    {"f",            "Zg=="},
    {"fo",           "Zm8="},
    {"foo",          "Zm9v"},
    {"foob",         "Zm9vYg=="},
    {"fooba",        "Zm9vYmE="},
    {"foobar",       "Zm9vYmFy"},
};

static BaseTest Base64TestURL[] =
{   
    {"",             ""},
    {"f",            "Zg=="},
    {"fo",           "Zm8="},
    {"foo",          "Zm9v"},
    {"foob",         "Zm9vYg=="},
    {"fooba",        "Zm9vYmE="},
    {"foobar",       "Zm9vYmFy"}
};

static BaseTest Base85TestMath[] =
{
    {"",             ""},
    {"f",            "1h"},
    {"fo",           "3RH"},
    {"foo",          "a{d/"},
    {"foob",         "w]zP%"},
    {"fooba",        "1edflI9"},
    {"foobar",       "3HRW{!&C"}
};

static BaseTest Base85Z85Test[] =
{
    {"",             ""},
    {"f",            "w="},
    {"fo",           "w]v"},
    {"foo",          "w]zO"},
    {"foob",         "w]zP%"},
    {"fooba",        "w]zP%ve"},
    {"foobar",       "w]zP%vr8"}
};

static BaseTest Base85AsciiTest[] =
{
    {"",             ""},
    {"f",            "Ac"},
    {"fo",           "Ao@"},
    {"foo",          "AoDS"},
    {"foob",         "AoDTs"},
    {"fooba",        "AoDTs@/"},
    {"foobar",       "AoDTs@<)"}
};

static BaseTest Base85TestPDF[] =
{
    /*{"",           "<~~>"},*/
    {"f",            "<~Ac~>"},
    {"fo",           "<~Ao@~>"},
    {"foo",          "<~AoDS~>"},
    {"foob",         "<~AoDTs~>"},
    {"fooba",        "<~AoDTs@/~>"},
    {"foobar",       "<~AoDTs@<)~>"},
};

static void RunGroup(const std::string& name, BaseTest* tests, size_t count, const base::BaseType& typeId)
{
    std::cout << "\n>>> Starting " << name << " Tests..." << std::endl;
    int passed = 0;

    for (size_t i = 0; i < count; i++) 
    {
        const auto& t = tests[i];

        // Подготовка входных данных
        std::vector<uint8_t> inputBin(t.input.begin(), t.input.end());
        std::string actualEnc;
        std::vector<uint8_t> actualDecBin;

        // Тест кодирования
        base::Encode(inputBin, actualEnc, typeId);

        // Тест декодирования
        bool decOk = base::Decode(actualEnc, actualDecBin, typeId);
        std::string actualDecStr(actualDecBin.begin(), actualDecBin.end());

        // Проверка
        bool ok = (actualEnc == t.expected) && decOk && (actualDecStr == t.input);
        if (ok) 
        {
            std::cout << "    [ OK ] => \"" << (t.input.empty() ? " " : t.input) << "\"" << std::endl;
            passed++;
        }
        else 
        {
            // ДЕБАГ ПРИ ОШИБКЕ
            std::cout << "    [FAIL] => \"" << t.input << "\"" << std::endl;
            std::cout << "    ---------------------------------------" << std::endl;
            if (actualEnc != t.expected) 
            {
                std::cout << "    ENCODE_MISMATCH:" << std::endl;
                std::cout << "           Expected: \"" << t.expected << "\" (len:" << t.expected.length() << ")" << std::endl;
                std::cout << "             Actual: \"" << actualEnc << "\" (len:" << actualEnc.length() << ")" << std::endl;
            }
            if (!decOk) 
            {
                std::cout << "    DECODE_FAILED: Function returned 'false'" << std::endl;
            }
            else if (actualDecStr != t.input) 
            {
                std::cout << "    DATA_CORRUPTION: Decoded string != Original input" << std::endl;
            }
            std::cout << "    ---------------------------------------" << std::endl;
        }
    }
}


int main() 
{  
    RunGroup("BASE-16",            Base16Test,           ARRAY_SIZE(Base16Test),           base::BaseType::BASE16);
    RunGroup("BASE-26",            Base26Test,           ARRAY_SIZE(Base26Test),           base::BaseType::BASE26);
    RunGroup("BASE-32",            Base32Test,           ARRAY_SIZE(Base32Test),           base::BaseType::BASE32);
    RunGroup("BASE-32-HEX",        Base32HEXTest,        ARRAY_SIZE(Base32HEXTest),        base::BaseType::BASE32_HEX);
    RunGroup("BASE-32-CROCKFORD",  Base32CrockfordTest,  ARRAY_SIZE(Base32CrockfordTest),  base::BaseType::BASE32_CROCKFORD);
	RunGroup("BASE-36",            Base36Test,           ARRAY_SIZE(Base36Test),           base::BaseType::BASE36);
    RunGroup("BASE-45",            Base45Test,           ARRAY_SIZE(Base45Test),           base::BaseType::BASE45);
	RunGroup("BASE-58-FLICKR",     Base58FlickrTest,     ARRAY_SIZE(Base58FlickrTest),     base::BaseType::BASE58_FLICKR);
    RunGroup("BASE-58-RIPPLE",     Base58RippleTest,     ARRAY_SIZE(Base58RippleTest),     base::BaseType::BASE58_RIPPLE);
    RunGroup("BASE-58-BITCOIN",    Base58BitcoinTest,    ARRAY_SIZE(Base58BitcoinTest),    base::BaseType::BASE58_BITCOIN);
    RunGroup("BASE-62",            Base62Test,           ARRAY_SIZE(Base62Test),           base::BaseType::BASE62);
    RunGroup("BASE-64",            Base64Test,           ARRAY_SIZE(Base64Test),           base::BaseType::BASE64);
    RunGroup("BASE-64-URL",        Base64TestURL,        ARRAY_SIZE(Base64TestURL),        base::BaseType::BASE64_URL);
    RunGroup("BASE-85-MATH",       Base85TestMath,       ARRAY_SIZE(Base85TestMath),       base::BaseType::BASE85_MATH);
    RunGroup("BASE-85-Z85",        Base85Z85Test,        ARRAY_SIZE(Base85Z85Test),        base::BaseType::BASE85_Z85);
    RunGroup("BASE-85-ASCII85",    Base85AsciiTest,      ARRAY_SIZE(Base85AsciiTest),      base::BaseType::BASE85_ASCII85);
    RunGroup("BASE-85-PDF",        Base85TestPDF,        ARRAY_SIZE(Base85TestPDF),        base::BaseType::BASE85_PDF);

    return 0;
}

/*
>>> Starting BASE-16 Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-26 Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-32 Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-32-HEX Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-32-CROCKFORD Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-36 Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-45 Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-58-FLICKR Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-58-RIPPLE Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-58-BITCOIN Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-62 Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-64 Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-64-URL Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-85-MATH Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-85-Z85 Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-85-ASCII85 Tests...
    [ OK ] => " "
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

>>> Starting BASE-85-PDF Tests...
    [ OK ] => "f"
    [ OK ] => "fo"
    [ OK ] => "foo"
    [ OK ] => "foob"
    [ OK ] => "fooba"
    [ OK ] => "foobar"

*/