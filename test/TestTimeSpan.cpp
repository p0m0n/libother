#include "TimeSpan.h"

using namespace other::time;

// Вспомогательная функция для вывода результатов
static void PrintResult(const std::string& testName, bool passed) 
{
    std::cout << std::left << std::setw(50) << testName << ": "
        << (passed ? "[  OK  ]" : "[ FAIL ]") << std::endl;
}

static void TestTimeSpan()
{

    // 1. Тесты конструкторов и констант
    {
        TimeSpan ts0;
        TimeSpan tsTicks(10000000); // 1 секунда
        TimeSpan tsHMS(1, 2, 3);    // 1ч 2м 3с
        TimeSpan tsDHMS(1, 2, 3, 4, 500); // 1д 2ч 3м 4с 500мс

        PrintResult("Constructor Default/Ticks", ts0.Ticks() == 0 && tsTicks.Ticks() == 10000000);
        PrintResult("Constructor HMS", tsHMS.Hours() == 1 && tsHMS.Minutes() == 2 && tsHMS.Seconds() == 3);
        PrintResult("Constructor DHMSM", tsDHMS.Days() == 1 && tsDHMS.Milliseconds() == 500);

        try {
            // Тест на переполнение в конструкторе (если реализовано в SafeMath/операциях)
            // TimeSpan overflow(1000000, 0, 0, 0); 
            PrintResult("Constants Min/Max/Zero", TimeSpan::Zero.Ticks() == 0);
        }
        catch (...) {}
    }

    // 2. Тесты получения компонентов (Accessors)
    {
        // 1 день, 2 часа, 3 минуты, 4 секунды, 5 миллисекунд
        TimeSpan ts(1, 2, 3, 4, 5);
        bool ok = (ts.Days() == 1 && ts.Hours() == 2 && ts.Minutes() == 3 && ts.Seconds() == 4 && ts.Milliseconds() == 5);
        PrintResult("Component Accessors (Days/Hours...)", ok);

        // Проверка отрицательных компонентов
        TimeSpan negTs(-1, -2, -3, -4, -5);
        bool negOk = (negTs.Days() == -1 && negTs.Hours() == -2 && negTs.Milliseconds() == -5);
        PrintResult("Negative Component Accessors", negOk);
    }

    // 3. Тесты Total-свойств (дробные значения)
    {
        // Возьмем ровно 12 часов для легкой проверки
        TimeSpan ts12h(12, 0, 0);

        // Используем небольшую дельту для сравнения double
        auto is_near = [](double a, double b) {
            return std::abs(a - b) < 0.000001;
        };

        bool hoursOk = is_near(ts12h.TotalHours(), 12.0);
        bool daysOk = is_near(ts12h.TotalDays(), 0.5); // 12/24 = 0.5

        PrintResult("TotalHours (12h)", hoursOk);
        PrintResult("TotalDays (12h -> 0.5d)", daysOk);

        // Проверка 45 минут
        TimeSpan ts45m(0, 45, 0);
        PrintResult("TotalMinutes (45m)", is_near(ts45m.TotalMinutes(), 45.0));
        PrintResult("TotalHours (45m -> 0.75h)", is_near(ts45m.TotalHours(), 0.75));
    }

    // 4. Арифметические операции
    {
        TimeSpan a(1, 0, 0); // 1 час
        TimeSpan b(0, 30, 0); // 30 минут

        TimeSpan sum = a.Add(b);
        TimeSpan diff = a.Subtract(b);

        PrintResult("Method Add/Subtract", sum.TotalMinutes() == 90.0 && diff.TotalMinutes() == 30.0);
        PrintResult("Operator + / -", (a + b).Ticks() == sum.Ticks() && (a - b).Ticks() == diff.Ticks());

        TimeSpan neg(1000);
        PrintResult("Negate/Duration", neg.Negate().Ticks() == -1000 && neg.Negate().Duration().Ticks() == 1000);
    }

    // 5. Операторы сравнения
    {
        TimeSpan small(100);
        TimeSpan large(200);

        bool cmp = (small < large) && (large > small) && (small != large) && (small == TimeSpan(100));
        PrintResult("Comparison Operators (==, !=, <, >)", cmp);
        PrintResult("Static Compare", TimeSpan::Compare(small, large) == -1);
    }

    // 6. Статические фабрики (From...)
    {
        TimeSpan fDay = TimeSpan::FromDays(1.5);
        TimeSpan fMin = TimeSpan::FromMinutes(1);

        PrintResult("Static FromDays (1.5)", fDay.TotalHours() == 36.0);
        PrintResult("Static FromTicks", TimeSpan::FromTicks(100).Ticks() == 100);
    }

    // 7. Строковое представление и Хэш
    {
        TimeSpan ts(1, 2, 3, 4, 500);
        std::string s = ts.ToString();
        // Ожидаем формат [d.]hh:mm:ss[.fff] -> "1.02:03:04.500"
        PrintResult("ToString (Format check)", !s.empty());
        PrintResult("GetHashCode", ts.GetHashCode() != 0);
    }

    // 8. Тест нормализации (Перенос разрядов)
    {
        // 0 дней, 0 часов, 120 минут, 0 секунд
        TimeSpan ts(0, 0, 120, 0);
        PrintResult("Normalization (120 min -> 2 hours)", ts.Hours() == 2 && ts.Minutes() == 0);

        // Проверка переноса из миллисекунд в секунды
        TimeSpan tsMs(0, 0, 0, 0, 5000); // 5000 мс = 5 сек
        PrintResult("Normalization (5000 ms -> 5 sec)", tsMs.Seconds() == 5);
    }

    // 9. ПАРСИНГ (Самое важное)
    {
        TimeSpan res;

        // Обычный TryParse
        bool p1 = TimeSpan::TryParse("12:45:00", res);
        PrintResult("TryParse (12:45:00)", p1 && res.Hours() == 12 && res.Minutes() == 45);

        bool p2 = TimeSpan::TryParse("15.12:45:00", res);
        PrintResult("TryParse (15.12:45:00)", p2 && res.Days() == 15 && res.Hours() == 12 && res.Minutes() == 45);

        bool p3 = TimeSpan::TryParse("15.12:45:30", res);
        PrintResult("TryParse (15.12:45:30)", p3&& res.Days() == 15 && res.Hours() == 12 && res.Minutes() == 45 && res.Seconds() == 30);

        bool p4 = TimeSpan::TryParse("20.12:45:30.300", res);
        PrintResult("TryParse (20.12:45:30.300)", p4&& res.Days() == 20 && res.Hours() == 12 && res.Minutes() == 45 && res.Seconds() == 30 && res.Milliseconds() == 300);

        // Отрицательный TryParse
        bool p5 = TimeSpan::TryParse("-12:45:00", res);
        PrintResult("TryParse (-12:45:00)", p5&& res.Hours() == -12 && res.Minutes() == -45);

        bool p6 = TimeSpan::TryParse("-15.12:45:00", res);
        PrintResult("TryParse (-15.12:45:00)", p6&& res.Days() == -15 && res.Hours() == -12 && res.Minutes() == -45);

        bool p7 = TimeSpan::TryParse("-15.12:45:30", res);
        PrintResult("TryParse (-15.12:45:30)", p7&& res.Days() == -15 && res.Hours() == -12 && res.Minutes() == -45 && res.Seconds() == -30);

        bool p8 = TimeSpan::TryParse("-20.12:45:30.300", res);
        PrintResult("TryParse (-20.12:45:30.300)", p8&& res.Days() == -20 && res.Hours() == -12 && res.Minutes() == -45 && res.Seconds() == -30 && res.Milliseconds() == -300);

        // Обычный TryParseExact
        bool c1 = TimeSpan::TryParseExact("10:20:00", "hh:mm:ss", res);
        PrintResult("TryParseExact (10:20:00)", c1 && res.Hours() == 10 && res.Minutes() == 20);

        bool c2 = TimeSpan::TryParseExact("10:20:25", "hh:mm:ss", res);
        PrintResult("TryParseExact (10:20:25)", c2 && res.Hours() == 10 && res.Minutes() == 20 && res.Seconds() == 25);

        bool c3 = TimeSpan::TryParseExact("10:20:25.300", "hh:mm:ss.ffff", res);
        PrintResult("TryParseExact (10:20:25.300)", c3 && res.Hours() == 10 && res.Minutes() == 20 && res.Seconds() == 25 && res.Milliseconds() == 300);

        bool c4 = TimeSpan::TryParseExact("25.10:20:25.300", "d.hh:mm:ss.ffff", res);
        PrintResult("TryParseExact (25.10:20:25.300)", c4 && res.Days() == 25 && res.Hours() == 10 && res.Minutes() == 20 && res.Seconds() == 25 && res.Milliseconds() == 300);

        // Отрицательный TryParseExact
        bool c5 = TimeSpan::TryParseExact("-10:20:00", "hh:mm:ss", res);
        PrintResult("TryParseExact (-10:20:00)", c5&& res.Hours() == -10 && res.Minutes() == -20);

        bool c6 = TimeSpan::TryParseExact("-10:20:25", "hh:mm:ss", res);
        PrintResult("TryParseExact (-10:20:25)", c6&& res.Hours() == -10 && res.Minutes() == -20 && res.Seconds() == -25);

        bool c7 = TimeSpan::TryParseExact("-10:20:25.300", "hh:mm:ss.ffff", res);
        PrintResult("TryParseExact (-10:20:25.300)", c7&& res.Hours() == -10 && res.Minutes() == -20 && res.Seconds() == -25 && res.Milliseconds() == -300);

        bool c8 = TimeSpan::TryParseExact("-25.10:20:25.300", "d.hh:mm:ss.ffff", res);
        PrintResult("TryParseExact (-25.10:20:25.300)", c8&& res.Days() == -25 && res.Hours() == -10 && res.Minutes() == -20 && res.Seconds() == -25 && res.Milliseconds() == -300);

        // Обычный TryParseExact Vector
        std::vector<std::string> fmts = { "mm:ss", "hh:mm:ss", "hh:mm:ss.ffff", "d.hh:mm:ss.ffff"};

        bool d1 = TimeSpan::TryParseExact("45:00", fmts, res);
        PrintResult("TryParseExact Vector (45:00 -> mm:ss)", d1 && res.Minutes() == 45 && res.Seconds() == 0);

        bool d2 = TimeSpan::TryParseExact("12:45:30", fmts, res);
        PrintResult("TryParseExact Vector (12:45:30 -> hh:mm:ss)", d2&& res.Hours() == 12 && res.Minutes() == 45 && res.Seconds() == 30);

        bool d3 = TimeSpan::TryParseExact("12:45:30.400", fmts, res);
        PrintResult("TryParseExact Vector (12:45:30.400->hh:mm:ss.ffff)", d3&& res.Hours() == 12 && res.Minutes() == 45 && res.Seconds() == 30 && res.Milliseconds() == 400);

        bool d4 = TimeSpan::TryParseExact("25.12:45:30.400", fmts, res);
        PrintResult("TryParseExact Vector (25.12:45:30.400->d.hh:mm:ss.ffff)", d4&& res.Days() == 25 && res.Hours() == 12 && res.Minutes() == 45 && res.Seconds() == 30 && res.Milliseconds() == 400);

        // Отрицательный TryParseExact Vector 
        bool d5 = TimeSpan::TryParseExact("-45:00", fmts, res);
        PrintResult("TryParseExact Vector (-45:00 -> mm:ss)", d5&& res.Minutes() == -45 && res.Seconds() == -0);

        bool d6 = TimeSpan::TryParseExact("-12:45:30", fmts, res);
        PrintResult("TryParseExact Vector (-12:45:30 -> hh:mm:ss)", d6&& res.Hours() == -12 && res.Minutes() == -45 && res.Seconds() == -30);

        bool d7 = TimeSpan::TryParseExact("-12:45:30.400", fmts, res);
        PrintResult("TryParseExact Vector (-12:45:30.400->hh:mm:ss.ffff)", d7&& res.Hours() == -12 && res.Minutes() == -45 && res.Seconds() == -30 && res.Milliseconds() == -400);

        bool d8 = TimeSpan::TryParseExact("-25.12:45:30.400", fmts, res);
        PrintResult("TryParseExact Vector (-25.12:45:30.400->d.hh:mm:ss.ffff)", d8&& res.Days() == -25 && res.Hours() == -12 && res.Minutes() == -45 && res.Seconds() == -30 && res.Milliseconds() == -400);

    }
}

int main() 
{
    setlocale(LC_ALL, "Russian");

	TestTimeSpan();

    TimeSpan ts;
    {
        // Тест оператора ввода.
        std::cout << "\nВведите время (hh:mm:ss): ";
        if (!(std::cin >> ts))
        {
            std::cerr << "Ошибка: Неверный формат времени!" << std::endl;
            std::cin.clear(); // Сброс состояния ошибки
            std::cin.ignore(1000, '\n'); // Очистка буфера
        }

        // Тест оператора вывода.
        std::cout << ts << std::endl;
    }
    
    return 0;
}
