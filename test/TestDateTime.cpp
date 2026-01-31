#include <iostream>
#include <cassert>
#include "DateTime.h"
#include <vector>
#include <iomanip>
#include <string>
#include <Windows.h>

using namespace other::time;

// Вспомогательная функция для вывода результатов
static void PrintResult(const std::string& testName, bool passed)
{
    std::cout << std::left << std::setw(50) << testName << ": "
        << (passed ? "[  OK  ]" : "[ FAIL ]") << std::endl;
}

void PrintAllFormats() 
{
    DateTime dt = DateTime::Now();
    std::vector<std::string> allFormats = dt.DateTimeFormats();

    std::cout << "Available formats for current DateTime:" << std::endl;
    for (const auto& s : allFormats) {
        std::cout << "  -> " << s << std::endl;
    }
}

struct fmt_test
{
    const char* token;
    const char* format;
};

static fmt_test formats_date_time[] = { 
    {"d","12.01.2026"},
    {"D","Monday, 12 January 2026"},
    {"t","19:24" },
    {"T","19:24:08"},
    {"g","12.01.2026 19:24"},
    {"G", "12.01.2026 19:24:08"},
    {"f","Monday, 12 January 2026 19:24"},
    {"F","Monday, 12 January 2026 19:24:08"},
    {"M","12 January"},
    {"Y","January 2026"},
    {"s", "2026-01-12T19:24:08"},
    {"u", "2026-01-12 19:24:08Z"},
    {"R","Mon, 12 Jan 2026 19:24:08 GMT"}};


static void TestsDateTime()
{
    // 1. Конструкторы и базовые свойства
    {
        DateTime dt0(2025, 12, 25);
        bool c0 = (dt0.Year() == 2025 && dt0.Month() == 12 && dt0.Day() == 25);
        PrintResult("Constructor (Y, M, D)", c0);

        DateTime dt1(2025, 12, 25, 10, 30, 0);
        bool c1 = (dt1.Year() == 2025 && dt1.Month() == 12 && dt1.Day() == 25 && dt1.Hour() == 10 && dt1.Minute() == 30);
        PrintResult("Constructor (Y, M, D, H, M, S)", c1);

        DateTime dt2(dt1.Ticks(), DateTimeKind::Utc);
        PrintResult("Constructor (Ticks, Kind)", dt2.Ticks() == dt1.Ticks() && dt2.Kind() == DateTimeKind::Utc);

        PrintResult("Constants (Min/Max/Zero)", DateTime::MinValue.Year() == 1 && DateTime::MaxValue.Year() == 9999);
    }

    // 2. Статические методы (Now, Today) и Календарь
    {
        DateTime today = DateTime::Today();
        PrintResult("Static Today (Time is Midnight)", today.Hour() == 0 && today.Minute() == 0);

        PrintResult("IsLeapYear (2000 - Yes, 1900 - No)", DateTime::IsLeapYear(2000) && !DateTime::IsLeapYear(1900));
        PrintResult("DaysInMonth (Feb 2024 - 29, Feb 2023 - 28)", DateTime::DaysInMonth(2024, 2) == 29 && DateTime::DaysInMonth(2023, 2) == 28);
    }

    // 3. Компоненты даты (DayOfWeek, DayOfYear)
    {
        // 1 января 2026 года (из контекста) — это Четверг
        DateTime dt(2026, 1, 1);
        PrintResult("GetDayOfWeek (2026-01-01 is Thursday)", (int)dt.GetDayOfWeek() == 4); // Thursday = 4
        PrintResult("DayOfYear (Jan 1st)", dt.DayOfYear() == 1);
        PrintResult("DayOfYear (Feb 1st non-leap)", DateTime(2026, 2, 1).DayOfYear() == 32);
    }

    // 4. Арифметика (Сложная логика месяцев и лет)
    {
        DateTime dt(2024, 1, 31); // Конец января
        DateTime nextMonth = dt.AddMonths(1);
        // Ожидаем 29 февраля, так как 2024 — високосный
        PrintResult("AddMonths (Jan 31 + 1 month -> Feb 29)", nextMonth.Month() == 2 && nextMonth.Day() == 29);

        DateTime leapDay(2024, 2, 29);
        DateTime nextYear = leapDay.AddYears(1);
        // Ожидаем 28 февраля 2025
        PrintResult("AddYears (Leap Day + 1 year -> Feb 28)", nextYear.Year() == 2025 && nextYear.Day() == 28);

        TimeSpan ts = TimeSpan::FromHours(25);
        DateTime afterTs = dt.Add(ts);
        PrintResult("Add(TimeSpan 25h)", afterTs.Day() == 1 && afterTs.Month() == 2 && afterTs.Hour() == 1);
    }

    // 5. Разность дат (Subtract)
    {
        DateTime d1(2026, 1, 2);
        DateTime d2(2026, 1, 1);
        TimeSpan diff = d1.Subtract(d2);
        PrintResult("Subtract(DateTime) -> TimeSpan", diff.TotalDays() == 1.0);
    }

    // 6. Конвертации (Local/UTC/Binary/Unix)
    {
        DateTime utc = DateTime::UtcNow();
        DateTime local = utc.ToLocalTime();
        PrintResult("ToLocalTime/ToUniversalTime", local.ToUniversalTime().Ticks() == utc.Ticks());

        // Binary Serialization (флаг Kind + Ticks)
        int64_t binary = utc.ToBinary();
        DateTime fromBinary = DateTime::FromBinary(binary);
        PrintResult("ToBinary/FromBinary (Persistence)", fromBinary.Ticks() == utc.Ticks() && fromBinary.Kind() == utc.Kind());

        // Unix Time
        DateTime unixEpoch(1970, 1, 1, 0, 0, 0, 0, DateTimeKind::Utc);
        PrintResult("ToUnixTimeSeconds (Epoch)", unixEpoch.ToUnixTimeSeconds() == 0);

        DateTime fromUnix = DateTime::FromUnixTimeSeconds(1704067200); // 2024-01-01
        PrintResult("FromUnixTimeSeconds", fromUnix.Year() == 2024 && fromUnix.Month() == 1 && fromUnix.Day() == 1);
    }

    // 7. Windows FileTime и OLE Date (Excel)
    {
        // 1 января 1601 года — начало эпохи FileTime
        DateTime ftStart(1601, 1, 1, 0, 0, 0, 0, DateTimeKind::Utc);
        PrintResult("ToFileTimeUtc (Start of Epoch)", ftStart.ToFileTimeUtc() == 0);

        // OLE Automation Date
        DateTime oaEpoch(1899, 12, 30, 0, 0, 0); // 0.0 в OLE
        PrintResult("ToOADate/FromOADate", oaEpoch.ToOADate() == 0.0 && DateTime::FromOADate(0.5).Hour() == 12);
    }

    // 8. Форматирование и Парсинг
    DateTime parsed;
    {
        DateTime dt(2026, 1, 8, 14, 30, 05);
        std::string s = dt.ToShortDateString();
        PrintResult("ToShortDateString (dd.MM.yyyy)", s == "08.01.2026");

        std::string custom = dt.ToString("yyyy-MM-dd HH:mm");
        PrintResult("ToString(format)", custom == "2026-01-08 14:30");

        bool p2 = DateTime::TryParseExact("2026/01/08", "yyyy/MM/dd", parsed);
        PrintResult("TryParseExact", p2 && parsed.Month() == 1);
    }

    // 9. Операторы
    {
        DateTime d1(2026, 1, 1);
        DateTime d2(2026, 1, 2);
        PrintResult("Operators (==, !=, <, >)", (d1 < d2) && (d1 != d2) && (d2 > d1));

        DateTime d3 = d1 + TimeSpan::FromDays(1);
        PrintResult("Operator DateTime + TimeSpan", d3 == d2);
    }

    std::cout << "\n" << std::endl;

    // 10. Парсинг
    DateTime parsed1;
    {
        for (int i = 0; i < ARRAYSIZE(formats_date_time); i++)
        {
            bool result = DateTime::TryParse(formats_date_time[i].format, parsed1);
            if (result)
            {
                printf("Token [%s] => %s\n", formats_date_time[i].token, parsed1.ToString(formats_date_time[i].token).c_str());
            }
        }
    }

}

int main() 
{
    setlocale(LC_ALL, "Russian");
    TestsDateTime();
    std::cout << "\n" << std::endl;
    PrintAllFormats();
	

    DateTime dt2;
    {
        // Тест оператора ввода.
        std::cout << "\nВведите время (hh:mm:ss): ";
        if (!(std::cin >> dt2))
        {
            std::cerr << "Ошибка: Неверный формат времени!" << std::endl;
            std::cin.clear(); // Сброс состояния ошибки
            std::cin.ignore(1000, '\n'); // Очистка буфера
        }

        // Тест оператора вывода.
        std::cout << dt2 << std::endl;
    }

    return 0;
}
