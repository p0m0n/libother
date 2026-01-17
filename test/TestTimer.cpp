#include <iostream>
#include <memory>
#include <vector>
#include <thread>
#include "Timer.h"

using namespace other::time;
using namespace other::time::timer;


// 1. Создаем действие, которое выполнится по таймеру
class BombTask : public ICallable
{
public:
    void callAfterTimeout() override
    {
        std::cout << "\n\n[BOOM] Таймер сработал! Задача выполнена." << std::endl;
    }
};

int main()
{
    setlocale(LC_ALL, "Russian");

    // 2. Создаем экземпляр
    Timer timerEngine;

    // 3. Подготавливаем параметры (сработка через 5 секунд, без повторов)
    DateTime start = DateTime::Now();
    DateTime end   = start + TimeSpan::FromSeconds(5);
    auto myTask    = std::make_shared<BombTask>();

    // 4. Создаем одиночный (без интервала) таймер и получаем его ID
    uint64_t id = timerEngine.CreateTimer(start, end, TimeSpan::Zero, myTask, "ExplosionTimer");

    // 5. Обязательно запускаем его (по умолчанию он на паузе)
    timerEngine.StartTimer(id);

    std::cout << "Таймер запущен на 5 секунд. Начинаем цикл проверки..." << std::endl;

    // 6. Главный цикл (имитация работы приложения)
    while (true)
    {
        // Обновляем логику таймера
        timerEngine.CheckTimeout(id);

        // Выводим статус в консоль
        TimeSpan remaining = timerEngine.RemainingTime(id);
        std::cout << "\rОсталось: " << remaining.TotalSeconds() << " сек.  " << std::flush;

        // Если таймер закончил работу — выходим из цикла
        // (Проверяем через список таймеров или флаги)
        if (timerEngine.TimerList().at(id)._finish)
        {
            break;
        }

        // Небольшая пауза, чтобы не нагружать процессор на 100%
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 7. Очищаем память от отработанных таймеров
    timerEngine.RemoveStopTimers();
    std::cout << "\nПрограмма завершена. Память очищена." << std::endl;

    return 0;
}
